#include "cache.h"

static void eliminate_lru_cache(cache_queue_t *q);
static void free_cache(cache_t *cache);

void init_cache_queue(cache_queue_t *q) {
	q->head = q->tail = NULL;
	q->total_size = 0;
	Sem_init(&q->reader_mutex, 0, 1);
	Sem_init(&q->writer_mutex, 0, 1);
	q->reader_cnt = 0;
}

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

void init_cache(cache_t *cache) {
	cache->size = 0;
	cache->prev = cache->next = NULL;
	cache->value = (char *) Malloc(sizeof(char) * MAX_OBJECT_SIZE);
	cache->next_write_pos = cache->value;
	cache->valid = true;
}

void cache_copy(cache_t *cache, char *buf, size_t size) {
	if (cache->size + size > MAX_OBJECT_SIZE) {
		cache->valid = false;
		return;
	}
	memcpy(cache->next_write_pos, buf, sizeof(char) * size);
	cache->size += size;
	cache->next_write_pos += size;
}

void realloc_cache(cache_t *cache) {
	Realloc(cache->value, cache->size);
}

static void free_cache(cache_t *cache) {
	if (cache != NULL) {
		Free(cache->value);
		Free(cache);
	}
}

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
