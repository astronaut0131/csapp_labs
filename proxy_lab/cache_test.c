#include "cache.h"

static int test_count = 0;
static int test_pass = 0;

#define EXPECT_EQ_BASE(equality, expect, actual, format) \
	do {\
		test_count++; \
		if (equality) \
			test_pass++; \
		else {\
			fprintf(stderr, "%s:%d: expect: " format " actual " format "\n",__FILE__, __LINE__, expect, actual); \
		} \
	} while (0)

#define EXPECT_EQ_STRING(expect,actual) EXPECT_EQ_BASE(expect == actual, expect, actual, "%d")
#define EXPECT_EQ_POINTER(expect,actual) EXPECT_EQ_BASE(expect == actual, (long unsigned int)expect, (long unsigned int)actual, "%lx")
#define EXPECT_EQ_INT(expect,actual) EXPECT_EQ_BASE(expect == actual, expect, actual, "%d")

int main() {
	cache_t *cache1 = (cache_t *) Malloc(sizeof(cache_t));
	cache_t *cache2 = (cache_t *) Malloc(sizeof(cache_t));
	cache_t *cache3 = (cache_t *) Malloc(sizeof(cache_t));
	cache_t *cache4 = (cache_t *) Malloc(sizeof(cache_t));
	cache_queue_t *cache_queue = (cache_queue_t *) Malloc(sizeof(cache_queue_t));
	init_cache(cache1);
	init_cache(cache2);
	init_cache(cache3);
	init_cache(cache4);
	init_cache_queue(cache_queue);
	strcpy(cache1->key, "key1");
	strcpy(cache2->key, "key2");
	strcpy(cache3->key, "key3");
	cache_copy(cache1, "value1", strlen("value1"));
	cache_copy(cache2, "value2", strlen("value2"));
	cache_copy(cache2, "value3", strlen("value3"));
	put_cache(cache_queue, cache1);
	put_cache(cache_queue, cache2);
	put_cache(cache_queue, cache3);
	EXPECT_EQ_INT(cache_queue->total_size, cache1->size + cache2->size + cache3->size);
	EXPECT_EQ_POINTER(cache1, cache_queue->head);
	EXPECT_EQ_POINTER(cache3, cache_queue->tail);
	EXPECT_EQ_POINTER(cache1->prev, NULL);
	EXPECT_EQ_POINTER(cache3->next, NULL);
	EXPECT_EQ_POINTER(cache2, find_cache_by_key(cache_queue, "key2"));
	move_to_queue_back(cache_queue, cache2);
	EXPECT_EQ_POINTER(cache1, cache_queue->head);
	EXPECT_EQ_POINTER(cache2, cache_queue->tail);
	EXPECT_EQ_POINTER(cache1->prev, NULL);
	EXPECT_EQ_POINTER(cache2->next, NULL);
	EXPECT_EQ_POINTER(cache3, find_cache_by_key(cache_queue, "key3"));
	cache4->size = MAX_CACHE_SIZE;
	put_cache(cache_queue,cache4);
	EXPECT_EQ_INT(cache_queue->total_size, cache4->size);
	EXPECT_EQ_POINTER(cache_queue->head, cache4);
	EXPECT_EQ_POINTER(cache_queue->tail, cache4);
	printf("test case passed (%d/%d)\n", test_pass, test_count);
	return 0;
}
