# 简介
做前先仔细阅读 writeup http://csapp.cs.cmu.edu/3e/shlab.pdf

感觉这个lab比较简单，基本遵循writeup里的hints就没啥问题

## 一些需要注意的地方
一开始我没仔细读writeup，在`waitfg`与`sigchld_handler`中都使用了`waitpid`，一个子进程结束时会调用`waitfg`中的`waitpid`，同时也会发出`sigchld`被`sigchld_handler`捕获，调用`sigchld_handler`的`waitpid`，处理不好就会造成某个`waitpid`阻塞，而且阻塞了用gdb也不太容易调，`ctrl-c`后`bt`无法看到是在哪个`waitpid`阻塞了，出现了很多难找的bug，遂放弃这种做法。

改用writeup中推荐的做法,在`waitfg`中不断调用`sleep`使shell主进程一直阻塞，不负责reap子进程，直到foreground子进程被reaped或stopped，所有子进程的reaping由`sigchld_handler`负责。

```
void sigchld_handler(int sig) 
{
	pid_t pid;
	int status;
	while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
		if (!WIFSTOPPED(status)) {
			deletejob(jobs,pid);
		}
	}
	if (errno != ECHILD)
		error_wrapper(pid, "waitpid error");
    return;
}
```

读完csapp教材上的内容可以知道，`waitpid`需要用`while`以防止有信号被丢弃，用了`while`我们就需要用`WNOHANG`，不然有一个子进程结束时，第一次循环会reap子进程，第二次循环时，没有等到结束的子进程的话，`waitpid`会一直阻塞直到有下一个子进程结束。`WUNTRACED`用于子进程被stopped时也使`waitpid`返回，注意若子进程是被stopped的话我们不需要把它从任务列表用移除，可以用`WIFSTOPPED`判断。综合以上这些，做错误处理的时候要注意，`while`结束后`errno`出现`ECHILD`是正常现象，`ECHILD`代表没有子进程错误。如果只有一个子进程，且在第一次循环这个子进程被收割了，下个循环的时候就一个子进程都没了。因此错误处理要把这种情况排除在外。
***
```
/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
	pid_t pid = fgpid(jobs);
	if (pid == 0) return;
	/* send signal to foreground group */
	/* in case child process has its own child process */
	error_wrapper(kill(pid*-1, sig),"kill error");
	printf("Job [%d] (%d) terminated by signal %d\n",pid2jid(pid),pid,sig); 
	deletejob(jobs, pid);
    return;
}
```

使用`kill`的时候注意对象得是foreground job进程组，试想创建的foreground job本身是一段多进程代码，这时候我们按下ctrl-c的时候应该结束子进程以及这个子进程中自己又开的进程，所以在`eval`的时候要给所有foreground job和background job分配各不相同的进程组id，一个不错的办法是把进程组id设为`pid`，这样子进程中如果有自己开的进程的话，这些进程都会继承子进程的进程id，即`pid`，后续我们使用子进程的`pid`就可以让这些进程全都收到信号。

***

```
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
	char *argv[MAXARGS];
	int bg;
	bg = parseline(cmdline, argv);
	if (argv[0] == NULL) return;	
	if (!strcmp(argv[0],"quit") || !strcmp(argv[0],"jobs")
		|| !strcmp(argv[0],"fg") || !strcmp(argv[0],"bg")) {
		builtin_cmd(argv);
	} else {
		/* block SIGCHLD */
		sigset_t set;
		error_wrapper(sigemptyset(&set),"sigemptyset error");
		error_wrapper(sigaddset(&set,SIGCHLD),"sigaddset error");
		error_wrapper(sigprocmask(SIG_BLOCK, &set, NULL),"sigprocmask error"); 
		pid_t pid = fork();
		error_wrapper(pid,"fork error");
		if (pid == 0) {
			/* child process inherit blocking sets from parent */
			/* we must unblock SIGCHLD in case child process has its own child process */
			error_wrapper(sigprocmask(SIG_UNBLOCK, &set, NULL),"sigprocmask error");
			if (execve(argv[0], argv, environ) == -1) {
				printf("%s: Command not found\n",argv[0]);
			}
			exit(0);
		}
		/* give each child process a seperate process group id*/
		/* if the child process has its own child process */
		/* they should also get interrupt or stop signal */
		error_wrapper(setpgid(pid,pid),"setpgid error");
		addjob(jobs, pid, bg ? 2 : 1, cmdline);
		if (bg) printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
		/* addjob and bg print must be done before chld signal */
		error_wrapper(sigprocmask(SIG_UNBLOCK, &set, NULL),"sigprocmask error");
		if (!bg) waitfg(pid);
	}
    return;
}
```
最后一个注意点教材上也有提到，即父进程子进程运行顺序是不定的，有可能出现这种情况：新开一个background job，这个job非常快，父进程还没来得及把它加入joblist，它就已经完成了并且发送`SIGCHLD`，试图将自己从joblist中删除，由此程序就会出现错误。解决的办法是先把`SIGCHLD`加入block set，等`addjob`等需要先进行的操作完成后，再把`SIGCHLD`从block set中移除，注意这边的block是指延迟信号的接收，使之处于pending的状态，而不是discard it。同时，子进程会继承父进程的block set，因此在子进程执行前需要把`SIGCHLD`从子进程的block set中移除，否则如果子进程中有它自己的进程执行的话，子进程将永远收不到这些进程执行完毕的信号。


其他基本没啥 make testxx慢慢调试就好。

