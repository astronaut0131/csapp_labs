# 简介
首先写`implicit free list`

`git checkout d507a2e69ffa27540406675b3379dc6d19041aff`

即csapp上的示例实现稍加改动
每个内存块的格式为
```
+--------+----------------+-----------------+--------+     
| header | payload        | alignment       | footer |    
+--------+----------------+-----------------+--------+    
| 8      | n              | align n to 8    | 8      |    
+--------+----------------+-----------------+--------+    
```
其中header和footer为
```
+------------------+--------+-------+
| block_total_size | no_use | alloc |
+------------------+--------+-------+
| 61 bits          | 2 bits | 1 bit |
+------------------+--------+-------+
```
注意`block_total_size`是指`header`,`8对齐后的payload大小`,`footer`加起来的总大小
这样组织的原因是：`block_total_size` 一定是8对齐的，如果我们用64位2进制表示它的话，它的最低三位一定是0（1，2，4不能形成8对齐），为了有效地利用
这最低三位，我们把这个块是否已经被占用的存在最低位，0表示空闲，1表示占用，其他两位没有用处。
宏的写法对应以上逻辑
```
/* get the size from a payload pointer */
#define GET_SIZE(ptr) (GET(HDRP(ptr)) & (~0x7))
/* get the alloced or not value from a payload pointer */
#define GET_ALLOC(ptr) (GET(HDRP(ptr)) & (0x1))
```
主要函数作用和思路：

`mm_init`: 申请第一块内存，并放置`prelogue block`和`epilogue block`标记堆的首尾方便遍历，当然维护静态全局变量标记首尾也是可以的，
	   这里我还初始化好了第一块 free block
	   
`place`: 用来放置块的函数，要占用一块空块，需要先考虑是不是要切开这个块（只占用我们需要的那部分，剩余的那部分独立成为一个free block），
         以防止internal fragmentation，这里是否需要切开块的条件可调参。这里实现的时候注意把块的大小，位置都搞明白。
	 
`mm_malloc`:首先要把目标`size`加上`header`,`footer`的大小进行8对齐，然后寻找合适的块（这里搜索的方式可调参，有`first fit`,`next fit`,`best fit`)，如果能找到一个空间>=对齐后size的块，就把它放置进去；如果找不到，用`mm_sbrk`移动堆的尾部
	    获得更多内存，放置，并移动`epilogue block`。
	    
`mm_free`:接受一个指针，清理这个指针对应的内存块。除了将header footer的alloc bit设为0， 还需要考虑这个空块是否能与它相领的空块进行合并，否则会产生
          external fragmentation，使用一个`coalesce`函数完成合并的功能。

`coalesce`:接受一个指针，检查指针对应的块的左右两个相邻块是否是空闲块，如果是的话进行合并。这里的实现就是分两边都不是空，左边是空右边不是空……四种情况，
	   需要注意的一个地方是在使用`PUT`摆新块的`header`,`footer`前需要先把总的`size`保存起来，不能`PUT`完再使用`GET_SIZE`相加去获得总`size`
	   ,因为`GET_SIZE`依赖原来的`header`,`footer`, `PUT`之后这里面的信息就变了。
	   
`mm_realloc`:接受一个指针和`size`,要求把指针对应的块的大小扩大到`size`,特殊情况怎么处理官方pdf里有些,这里只说扩大怎么做。
	     老规矩先求总大小并对齐，然后像`coalesce`一样考虑，如果相邻的有空闲块并且加在一起的大小能满足要求，我们就不需要申请新内存了，
	     这里需要写个`mm_memcpy`函数用于内存拷贝，实现的时候要判断一下`src`,`dst`的前后顺序并做相应处理防止数据被覆盖。如果没有相邻空闲块能满足要求，
	     这就需要调用`mm_malloc`，并把数据拷过去，把原来的块清除。
	     
基本读懂了书上示例代码就能有思路写，但要完全写对还是要花点时间调试的。。(不要照着书抄啊喂

调试的时候可以在Makefile里加上`-g -O0`，然后用gdb和`printf`调。。可以写个遍历整个block list打印所有信息的函数，各处调用一下。

这个naive的实现得分比较惨淡 `Perf index = 47 (util) + 10 (thru) = 57/100`

接下来优化的方向是减少搜索合适块的时间

`git checkout d507a2e69ffa27540406675b3379dc6d19041aff`


下一步是用书上介绍的`explicit double linked free list`,即空闲block的payload部分可以用来存储上一个节点，下一个节点信息,同时用一个全局静态变量`linked_list_head`作为链表头。

这样`best_fit`的搜索时间就从所有块变为了所有空闲的块
```
   +--------+----------------+-----------------+----------------------+--------+       
   | header | prev free addr | next free addr  | free payload aligned | footer |    
   +--------+----------------+-----------------+----------------------+--------+    
   | 8      | 8              | 8               | n                    | 8      |    
   +--------+----------------+-----------------+----------------------+--------+    
```
这样子的得分是 `Perf index = 47 (util) + 40 (thru) = 87/100`

****

原来40分thru已经是满分了。。不知道官方测试用的什么CPU。。
我用的服务器的cpu
```
model name	: Intel(R) Xeon(R) CPU E5-26xx v4
stepping	: 1
microcode	: 0x1
cpu MHz		: 2394.446
```
那接下来就是要减少fragmentation呗

`git checkout 3eff767d53467704db67c85914109e5f43da5b62`

```
Results for mm malloc:
trace  valid  util     ops      secs  Kops
 0       yes   99%    5694  0.000186 30646
 1       yes   99%    5848  0.000181 32309
 2       yes   99%    6648  0.000264 25230
 3       yes   99%    5380  0.000162 33128
 4       yes   66%   14400  0.000175 82286
 5       yes   96%    4800  0.002579  1861
 6       yes   94%    4800  0.002514  1909
 7       yes   61%   12000  0.036083   333
 8       yes   47%   24000  0.132992   180
 9       yes   48%   14401  0.001068 13488
10       yes   45%   14401  0.000674 21373
Total          78%  112372  0.176877   635
```
可以看到用一条双向链表的时候 `binary-bal.rep` 的利用率比较差。打开发现它里面的内存变化过程是：分配64 分配448 分配64 分配448 …… 清除所有448
分配512 分配512 ……，按现在写的代码这样就会导致严重的external fragmentation，所有空块都被64分割导致512一直找不到合适的空块。

这里想到了一个骚主意：

原来的`place`代码是：

```
static void place(void *ptr, size_t size, size_t blk_size) {
	/* place assume ptr points to a free block */
	remove_from_linked_list(ptr);
	if (blk_size >= size + SIZE_T_SIZE * 5) {
		PUT(HDRP(ptr),PACK(size,1));
		PUT(FTRP(ptr),PACK(size,1));
		insert_to_linked_list_head(NEXT_BLK(ptr));
		PUT(HDRP(NEXT_BLK(ptr)),PACK(blk_size - size,0));
		PUT(FTRP(NEXT_BLK(ptr)),PACK(blk_size - size,0));
	} else {
		PUT(HDRP(ptr),PACK(blk_size,1));
		PUT(FTRP(ptr),PACK(blk_size,1));
	}
}
```

可以发现所有被allocate的块都被放到了左边， 剩下的切出来的free block都被放到了右边。照这个逻辑`binary-bal.rep`就会出现|64|448|64|448|……
如果我们能把64的块聚在一块 448的块聚在一块 就能解决这个问题。

于是想到，调用`place`奇数次数时，把allocated块放在左边，切出来的free block放在右边；调用`place`偶数次数时，把allocated块放在右边， 切出来的free block放在左边。这样的话无论用户是隔着free还是一个一个free都不会造成严重的external fragmentation了。

于是把`place`的代码改成如下：
```
static void* place(void *ptr, size_t size, size_t blk_size, bool is_ptr_free) {
	if (is_ptr_free)
		remove_from_linked_list(ptr);
	if (blk_size >= size + min_malloc_size ) { 
		if (cnt % 2 == 0) {
			PUT(HDRP(ptr),PACK(size,1));
			PUT(FTRP(ptr),PACK(size,1));
			PUT(HDRP(NEXT_BLK(ptr)),PACK(blk_size - size,0));
			PUT(FTRP(NEXT_BLK(ptr)),PACK(blk_size - size,0));
			insert_to_linked_list_head(NEXT_BLK(ptr));
			cnt++;
			return ptr;
		} else {	
			PUT(HDRP(ptr),PACK(blk_size - size,0));
			PUT(FTRP(ptr),PACK(blk_size - size,0));
			PUT(HDRP(NEXT_BLK(ptr)),PACK(size,1));
			PUT(FTRP(NEXT_BLK(ptr)),PACK(size,1));
			insert_to_linked_list_head(ptr);
			cnt++;
			return NEXT_BLK(ptr);
		}
	} else {
		PUT(HDRP(ptr),PACK(blk_size,1));
		PUT(FTRP(ptr),PACK(blk_size,1));
		return ptr;
	}
}
```

同样思想解决realloc两个文件利用率低。

终于到达90分：
```
trace  valid  util     ops      secs  Kops
 0       yes   99%    5694  0.000229 24897
 1       yes   99%    5848  0.000223 26201
 2       yes   99%    6648  0.000315 21091
 3       yes   99%    5380  0.000198 27144
 4       yes   66%   14400  0.000269 53571
 5       yes   95%    4800  0.002561  1874
 6       yes   95%    4800  0.002650  1811
 7       yes   91%   12000  0.019426   618
 8       yes   79%   24000  0.009773  2456
 9       yes  100%   14401  0.001293 11134
10       yes   53%   14401  0.000196 73662
Total          89%  112372  0.037133  3026

Perf index = 53 (util) + 40 (thru) = 93/100
```

***
又尝试写了segregated free list
```
trace  valid  util     ops      secs  Kops
 0       yes   97%    5694  0.001033  5511
 1       yes   97%    5848  0.001071  5458
 2       yes   98%    6648  0.001143  5819
 3       yes   99%    5380  0.000880  6111
 4       yes   98%   14400  0.001698  8482
 5       yes   92%    4800  0.000991  4846
 6       yes   91%    4800  0.000926  5184
 7       yes   92%   12000  0.002838  4229
 8       yes   80%   24000  0.004637  5176
 9       yes   58%   14401  0.002439  5904
10       yes   97%   14401  0.001599  9007
Total          91%  112372  0.019254  5836

Perf index = 54 (util) + 40 (thru) = 94/100
```

Kops反而变低了。。。一定是我代码写的太垃圾了 自闭了
 
