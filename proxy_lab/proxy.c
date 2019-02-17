#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

static cache_queue_t *cache_queue;

typedef struct sockaddr SA;

typedef struct {
	char hostname[MAXLINE];
	char port[MAXLINE];
	char query[MAXLINE];
} uri_t; 

void clienterror(int fd, char *cause, char *errnum,
				 char *shortmsg, char *longmsg);

void debug_print(const char* msg) {
#ifdef DEBUG
	printf("[DEBUG] %s\n");
#endif
}

int parse_uri(char *uri,uri_t *uri_item) {
	char *query_delim, *delim = NULL, debug_msg[MAXLINE];
	if (strstr(uri,"http://") != uri)
		return -1;
	uri += strlen("http://");
	query_delim = strchr(uri,'/');
	*query_delim = '\0';
	uri_item->query[0] = '/';
	strcpy(uri_item->query + 1, query_delim + 1);
	/* specify port explicitly */
	if ((delim = strchr(uri,':'))) {
		*delim = '\0';
		strcpy(uri_item->port, delim + 1);
	} else {
		strcpy(uri_item->port, "80");
	}
	strcpy(uri_item->hostname, uri);
	sprintf(debug_msg,"parse uri result: hostname %s\tport %s\t query %s\n",
			uri_item->hostname, uri_item->port, uri_item->query);
	debug_print(debug_msg);
	return 0;
}

void *do_proxy(void *vargp) {
	rio_t client_rio, server_rio;
	int serverfd, clientfd = *((int *)vargp);
	char buf[MAXLINE], method[MAXLINE],
         uri[MAXLINE], version[MAXLINE], uri_copy[MAXLINE];
	size_t content_len = 0, n;
	Pthread_detach(pthread_self());
	Free(vargp);
	Rio_readinitb(&client_rio, clientfd);
	Rio_readlineb(&client_rio, buf, MAXLINE);
	sscanf(buf,"%s %s %s", method, uri, version);
	strcpy(uri_copy, uri);
	if (strcasecmp(method,"GET")) {
		clienterror(clientfd, method, "501", "Not implemented",
						"Proxy does not impletment this method");
		return NULL;
	}
	uri_t uri_item;
	if (parse_uri(uri, &uri_item) < 0) {	
		clienterror(clientfd, uri, "400", "Bad Request",
						"Proxy fail to parse request uri");
		return NULL;
	}
	cache_t *cache;
	/* cache hit*/
	if ((cache = find_cache_by_key(cache_queue, uri_copy))) { 
		Rio_writen(clientfd, cache->value, cache->size);
		/* it's the most recently used cache, move it to queue tail */
		move_to_queue_back(cache_queue, cache);
	} else {
		serverfd = Open_clientfd(uri_item.hostname, uri_item.port);
		Rio_readinitb(&server_rio, serverfd);
		sprintf(buf, "GET %s HTTP/1.0\r\n", uri_item.query);
		sprintf(buf, "%sHost: %s\r\n", buf, uri_item.hostname);
		sprintf(buf, "%s%s", buf, user_agent_hdr);
		sprintf(buf, "%sConnection: close\r\nProxy-Connection: close\r\n\r\n", buf);
		Rio_writen(serverfd, buf, strlen(buf));
		/* \r\n\r\n split header and entity */
		cache = (cache_t *) Malloc(sizeof(cache_t));
		init_cache(cache);
		strcpy(cache->key, uri_copy);
		Rio_readlineb(&server_rio, buf, MAXLINE);
		cache_copy(cache, buf, strlen(buf));
		Rio_writen(clientfd, buf, strlen(buf));
		while(strcmp(buf,"\r\n")) {
			Rio_readlineb(&server_rio, buf, MAXLINE);
			cache_copy(cache, buf, strlen(buf));
			Rio_writen(clientfd, buf, strlen(buf));
			char *delim = strchr(buf, ' ');
			if (delim) *delim = '\0';
			if (!strcasecmp(buf, "Content-length:")) {
				content_len = atoi(delim+1);
			}
		}
		while(content_len > 0 && (n = Rio_readnb(&server_rio, buf, MAXLINE))) { 
			cache_copy(cache, buf, n);
			Rio_writen(clientfd, buf, n);
			content_len -= n;
		}
		if (cache->valid) {
			/* shrink the unused space */
			realloc_cache(cache);
			put_cache(cache_queue, cache);
		}	
	}
	Close(serverfd);
	Close(clientfd);
	return NULL;
}

void clienterror(int fd, char *cause, char *errnum,
				 char *shortmsg, char *longmsg) {
	char buf[MAXLINE], body[MAXBUF];
	/* Build the HTTP response body */
	sprintf(body, "<html><title>Proxy Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	
	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}

int main(int argc,char **argv)
{
	int *connfd, listen_fd;
	struct sockaddr_in clientaddr;
	unsigned int clientlen = sizeof(clientaddr);
	struct hostent *hp;
	char *haddrp, debug_msg[MAXLINE];
	pthread_t tid;
	if (argc != 2) {
		printf("usage: %s port\n", argv[0]);
	}
	cache_queue_t cache_q;
	cache_queue = &cache_q;
	init_cache_queue(cache_queue);
	listen_fd = Open_listenfd(argv[1]);
	printf("listening on %s\n",argv[1]);
	while(1) {
		/* for thread safe */
		connfd = Malloc(sizeof(int));
		*connfd = Accept(listen_fd, (SA *)&clientaddr, &clientlen);
		/* get the DNS host entry structure */
		hp = Gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
						   sizeof(clientaddr.sin_addr.s_addr), AF_INET);
		/* get the IP string */
		haddrp = inet_ntoa(clientaddr.sin_addr);
		sprintf(debug_msg,"receive connection from %s (%s)\n", hp->h_name,haddrp);
		debug_print(debug_msg);
		Pthread_create(&tid, NULL, do_proxy, connfd);
	}
    return 0;
}
