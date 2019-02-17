#include "csapp.h"
#include <stdbool.h>
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

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
void init_cache(cache_t *cache);

void cache_copy(cache_t *cache, char *buf, size_t size);

void init_cache_queue(cache_queue_t *q);

void put_cache(cache_queue_t *q, cache_t *cache);

void move_to_queue_back(cache_queue_t *q, cache_t *cache);

void realloc_cache(cache_t *cache);

cache_t *find_cache_by_key(cache_queue_t *q, const char* key);
