# 简介
这个lab总体也不难，打log加`curl`调试很快能完成基本功能

中间碰到的问题是我试图用`%s`把`Rio_readnb`得到的内容打出开看看，然后出现了一些奇怪的错误，后来分析发现`Rio_readnb`是面向binary data的，不会给读到的最后一个字符后面补上`\0`，这是合理的，因为binary data中可能会包含很多`\0`，想对它使用`string`的一些方法并没有什么意义。而`Rio_readlineb`是面向
text的，即会在末尾补`\0`，可以对读到的内容使用`string`的方法。当然这些内容在writeup和textbook里都有强调。。

接下来讲一下cache的设计

```
typedef struct cache_struct{
	char key[MAXLINE];
	char* value;
	int size;
	struct cache_struct *prev;
	struct cache_struct *next;
	char* next_write_pos;
	bool valid;
} cache_t;

typedef struct {
	cache_t *head;
	cache_t *tail;
	int total_size;
	int reader_cnt;
	sem_t reader_mutex;
	sem_t writer_mutex;
} cache_queue_t;
```

要求cache替换使用LRU算法，那么自然想到的是维护一个FIFO队列，每次淘汰队首的cache，新进入的cache放在队尾，如果有一个cache已经在队列中了并且刚被访问，那就要把它移动到队尾。

```
void init_cache(cache_t *cache) {
	cache->size = 0;
	cache->prev = cache->next = NULL;
	cache->value = (char *) Malloc(sizeof(char) * MAX_OBJECT_SIZE);
	cache->next_write_pos = cache->value;
	cache->valid = true;
}
```

一开始给cache的`value`分配`MAX_OBJECT_SIZE`大小内存空间，用于accumulate读到的数据

```
void cache_copy(cache_t *cache, char *buf, size_t size) {
	if (cache->size + size > MAX_OBJECT_SIZE) {
		cache->valid = false;
		return;
	}
	memcpy(cache->next_write_pos, buf, sizeof(char) * size);
	cache->size += size;
	cache->next_write_pos += size;
}
```

可以看到这里`valid`的作用是用于判断最后cache的总数据大小是否超过了`MAX_OBJECT_SIZE`
```
void realloc_cache(cache_t *cache) {
	Realloc(cache->value, cache->size);
}
```
如果`valid`为`true`的话最后可以调用`Realloc`缩减一下`value`的内存空间
```
/* this is a helper funciton, pop out the least recently used cache */
static void eliminate_lru_cache(cache_queue_t *q) {
	if (q->head == NULL) return;
	q->total_size -= q->head->size;
	cache_t *copy = q->head;
	if (q->head == q->tail) {
		q->head = q->tail = NULL;
	} else {
		q->head = q->head->next;
		q->head->prev = NULL;
	}
	free_cache(copy);
}
```
淘汰least recently used的cache
```
void move_to_queue_back(cache_queue_t *q, cache_t *cache) {
	P(&q->writer_mutex);
	if (q->head == q->tail) {
		q->head = q->tail = NULL;
	} else if (q->head == cache) {
		q->head = q->head->next;
		q->head->prev = NULL;
	} else if (q->tail == cache) {
		q->tail = cache->prev;
		q->tail->next = NULL;
	} else {
		cache->prev->next = cache->next;
		cache->next->prev = cache->prev;
	}
	q->total_size -= cache->size;
	V(&q->writer_mutex);
	put_cache(q, cache);
}
```
把刚被读过的已经在队列中的cache放到队尾
```
cache_t *find_cache_by_key(cache_queue_t *q, const char* key) {
	P(&q->reader_mutex);
	/* q->reader_cnt is a shared variable for all readers and should be protected by mutex */
	q->reader_cnt++;
	/* first reader, start of reader sequence */
	if (q->reader_cnt == 1)
		/* forbid write operation while readers are reading */
		P(&q->writer_mutex);
	V(&q->reader_mutex);
	cache_t *result = NULL;
	for (cache_t *cursor = q->head; cursor != NULL; cursor = cursor->next) {
		if (!strcmp(cursor->key, key)) {
			result = cursor;
			break;
		}
	}
	P(&q->reader_mutex);
	/* current reader finish reading */
	q->reader_cnt--;
	/* all readers have finish reading, now it's writer's turn */
	if (q->reader_cnt == 0)
		V(&q->writer_mutex);
	V(&q->reader_mutex);
	return result;
}
```
这里的读写者问题使用的是书上介绍的代码，挺好理解的，其他会改变队列内容的操作都要加上写锁。

```
/* put_cache function assumes cache is in q and cache's size <= MAX_OBJECT_SIZE */
void put_cache(cache_queue_t *q, cache_t *cache) {
	P(&q->writer_mutex);
	if (q->head == NULL && q->tail == NULL) {
		cache->next = cache->prev = NULL;
		q->head = q->tail = cache;
	} else {
		/* swap out the victim cache if there is no space */
		while (q->total_size + cache->size > MAX_CACHE_SIZE) {
			eliminate_lru_cache(q);
		}
		if (q->head == NULL && q->tail == NULL) {
			q->head = q->tail = cache;
			cache->prev = cache->next = NULL;
		}
		else {
			q->tail->next = cache;
			cache->prev = q->tail;
			cache->next = NULL;
			q->tail = cache;
		}
	}
	q->total_size += cache->size;
	V(&q->writer_mutex);
}
```
把cache块放到队尾，注意如果要放置的这块cache + 队列中已有cache的总大小 > MAX_CACHE_SIZE时，需要不断调用`eliminate_lru_cache`直到空间足够再把这块cache放入，同时也注意`free`掉被淘汰的cache的内存空间。
		
		
