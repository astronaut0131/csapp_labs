/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "team Trump",
    /* First member's full name */
    "GuofuFeng",
    /* First member's email address */
    "gffeng@shou.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
/* get 8 bytes data from starting address p */
#define GET(p) (*(long*)(p))
/* put 8 bytes data to starting address p */
#define PUT(p,val) (*(long*)(p) = (val))
/* pack size and alloc into 4 bytes data*/
#define PACK(size,alloc) ((size) | (alloc))
/* get the size from a payload pointer */
#define GET_SIZE(ptr) (GET(HDRP(ptr)) & (~0x7))
/* get the alloced or not value from a payload pointer */
#define GET_ALLOC(ptr) (GET(HDRP(ptr)) & (0x1))
/* get the header pointer from a payload pointer */
#define HDRP(bp) ((bp) - SIZE_T_SIZE)
/* get the footer pointer from a payload pointer */
#define FTRP(bp) ((bp) + GET_SIZE(bp) - 2 * SIZE_T_SIZE)
/* get the payload pointer of next block */
#define NEXT_BLK(bp) ((bp) + GET_SIZE(bp))
/* get the payload pointer of previous block */
#define PREV_BLK(bp) ((bp) - (GET(bp - 2 * SIZE_T_SIZE) & (~0x7)))
/* the minimium size to call sbrk with */
#define CHUNK_SIZE (ALIGN(mem_pagesize()))
/* payload pointer of the prelogue block */
#define PRELOGUE_BLK (mem_heap_lo() + SIZE_T_SIZE)
/* get start address of the heap */
#define HEAP_START (mem_heap_lo())
/* get end address of the heap */
#define HEAP_END (mem_heap_hi() + 1)
/* get the payload pointer of previous free block */
#define PREV_FREE_BLK(bp) ((void *)(*(long *)(bp)))
/* get the payload pointer of next free block */
#define NEXT_FREE_BLK(bp) ((void *)(*(long *)(bp + SIZE_T_SIZE)))
/* set the payload pointer of previous free block to ptr */
#define SET_PREV_FREE_BLK(bp,ptr) (*(long *)(bp) = (long)(ptr))
/* set the payload pointer of next free block to ptr */
#define SET_NEXT_FREE_BLK(bp,ptr) (*(long *)(bp + SIZE_T_SIZE) = (long)(ptr))
/* for free block */
/*    +--------+----------------+-----------------+--------------+--------+    */   
/*    | header | prev free addr | next free addr  | free payload | footer |    */
/*    +--------+----------------+-----------------+--------------+--------+    */
/*    | 8      | 8              | 8               | n            | 8      |    */
/*    +--------+----------------+-----------------+--------------+--------+    */
/* head of the explicit free block linked list */
static void *linked_list_head = NULL;
static void print_heap(char  *);
static void insert_to_linked_list_head(void *);
static void remove_from_linked_list(void *);
static void* linked_list_first_fit(size_t);
static void* place(void *, size_t, size_t, bool);
static void *first_fit(size_t);
static void *best_fit(size_t);
void coalesce(void *);
static void mm_memcpy(void *, void *, size_t);
static int cnt = 0;
static size_t min_malloc_size = 5 * SIZE_T_SIZE;
/*
 *  print_heap - A debug helper function that traverse the heap and print information of all heaps
 *  msg is the addtional information to figure out where this function is called
 *  e.g. print_heap("heap status after calling coalesce")
 */
static void print_heap(char *msg) {
	if (msg[0] != '\0')
		printf("%s\n", msg);
	printf("Heap status -------------------------------\n");
	for (void *ptr = PRELOGUE_BLK; GET_SIZE(ptr) != 0; ptr = NEXT_BLK(ptr)) {
		printf("payload start = %lx payload end = %lx alloc = %ld size_header %d size_footer %d\n",
				(long)ptr, (long)FTRP(ptr), GET_ALLOC(ptr), (*(long *)HDRP(ptr)) & (~0x7), (*(long*)FTRP(ptr)) & (~0x7));
	}
}

/* 
 * print_linked_list - A debug helper function that traverse the free block linked list and print information
 */
static void print_linked_list(char *msg) {
	if (msg[0] != '\0')
		printf("%s\n", msg);
	printf("Linked list status -------------------------------\n");
	for (void* ptr = linked_list_head; ptr != NULL; ptr = NEXT_FREE_BLK(ptr)) {
		printf("prev: %lx current: %lx next: %lx\n", (long)PREV_FREE_BLK(ptr), (long)ptr, (long)NEXT_FREE_BLK(ptr));
	}
}

/*
 * insert_to_linked_list_head - insert a free block pointer ptr to the head of the free block linked list
 */
static void insert_to_linked_list_head(void *ptr) {
	if (linked_list_head == NULL) {
		SET_PREV_FREE_BLK(ptr,0);
		SET_NEXT_FREE_BLK(ptr,0);
	} else {
		SET_PREV_FREE_BLK(linked_list_head, ptr);
		SET_NEXT_FREE_BLK(ptr, linked_list_head);
		SET_PREV_FREE_BLK(ptr, 0);
	}
	linked_list_head = ptr;
}

/*
 * remove_from_linked_list - remove a free block's pointer information from the free block linked list
 */
static void remove_from_linked_list(void *ptr) {
	if (PREV_FREE_BLK(ptr) == NULL && NEXT_FREE_BLK(ptr) == NULL) {
		linked_list_head = NULL;
	} else if (PREV_FREE_BLK(ptr) != NULL && NEXT_FREE_BLK(ptr) == NULL) {
		SET_NEXT_FREE_BLK(PREV_FREE_BLK(ptr), NULL);
	} else if (PREV_FREE_BLK(ptr) == NULL && NEXT_FREE_BLK(ptr) != NULL) {
		SET_PREV_FREE_BLK(NEXT_FREE_BLK(ptr), NULL);
		linked_list_head = NEXT_FREE_BLK(ptr);
	} else {	
		SET_NEXT_FREE_BLK(PREV_FREE_BLK(ptr), NEXT_FREE_BLK(ptr));
		SET_PREV_FREE_BLK(NEXT_FREE_BLK(ptr), PREV_FREE_BLK(ptr));
	}
}

/*
 * linked_list_best_fit - using the free block linked list to find the best free block for size_t size
 */
static void* linked_list_best_fit(size_t size) {
	if (linked_list_head == NULL) {
		return NULL;
	}
	if (NEXT_FREE_BLK(linked_list_head) == NULL && PREV_FREE_BLK(linked_list_head) == NULL) {
		if (GET_SIZE(linked_list_head) >= size) return linked_list_head;
		else return NULL;
	}
	size_t min_size = (1 << 30);
	void *min_pos = NULL;
	for (void *cursor = linked_list_head; cursor != NULL; cursor = NEXT_FREE_BLK(cursor)) {
		if (GET_SIZE(cursor) >= size && GET_SIZE(cursor) < min_size) {
			min_size = GET_SIZE(cursor);
			min_pos = cursor;
		}
	}
	return min_pos;
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
	void *start_addr = mem_sbrk(CHUNK_SIZE);
	if (start_addr == (void *) -1) return -1;
	/* put the prelogue block */
	PUT(start_addr, PACK(2*SIZE_T_SIZE,1));
	PUT(start_addr + SIZE_T_SIZE, PACK(2*SIZE_T_SIZE,1));
	/* put the epilogue block */
	PUT(HEAP_END - SIZE_T_SIZE, PACK(0,1));
	/* put the free block between prelogue and epilogue */
	PUT(start_addr + SIZE_T_SIZE * 2, PACK(CHUNK_SIZE - 3 * SIZE_T_SIZE,0));
	PUT(HEAP_END - SIZE_T_SIZE * 2, PACK(CHUNK_SIZE - 3 * SIZE_T_SIZE,0));
	/* set linked list head to NULL cauz mm_init can be called more than once */
	linked_list_head = NULL;
	/* add linked list information to the block */
	insert_to_linked_list_head(start_addr + SIZE_T_SIZE * 3);
	return 0;
}

/*
 * linked_list_first_fit - using the free block linked list to find the first fit free block
 * the latest freed block is inserted to the head of the list
 */
static void* linked_list_first_fit(size_t size) {
	if (linked_list_head == NULL) {
		return NULL;
	}
	if (NEXT_FREE_BLK(linked_list_head) == NULL && PREV_FREE_BLK(linked_list_head) == NULL) {
		if (GET_SIZE(linked_list_head) >= size) return linked_list_head;
		else return NULL;
	}
	for (void *cursor = linked_list_head; cursor != NULL; cursor = NEXT_FREE_BLK(cursor)) {
		if (GET_SIZE(cursor) >= size) return cursor;
	}
	return NULL;
}

/*
 * place - Occupy a free block with blk_size to satisfy with a request of size
 * size - the request size
 * blk_size - the total size that ptr contains
 *
 * Explicitly using blk_size as a parameter instead of GET_SIZE(ptr) is for
 * cases in mm_realloc
 */
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


/*
 * first_fit -Find the first block that satisfy with the request of size, return NULL if not found
 * this is used in implicit free list
 */

static void *first_fit(size_t size) {
	for (void* ptr = PRELOGUE_BLK; GET_SIZE(ptr) != 0; ptr = NEXT_BLK(ptr)) {
		if (!GET_ALLOC(ptr) && GET_SIZE(ptr) >= size) {
			return ptr;
		}
	}
	return NULL;
}

/*
 * best_fit -Find the block with least free payload size that satisfy with the request of size, return NULL if not found
 * this is used in implicit free list
 */
static void *best_fit(size_t size) {
	size_t min_size = 1 << 30;
	void *min_ptr = NULL;
	for (void *ptr = PRELOGUE_BLK; GET_SIZE(ptr) != 0; ptr = NEXT_BLK(ptr)) {
		if (!GET_ALLOC(ptr) && GET_SIZE(ptr) >= size && GET_SIZE(ptr) < min_size) {
			min_size = GET_SIZE(ptr);
			min_ptr = ptr;
		}
	}
	return min_ptr;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
	/* add size with header and footer size, and align it */
	size_t new_size = ALIGN(size + 2 * SIZE_T_SIZE);
	min_malloc_size = min_malloc_size < new_size ? min_malloc_size : new_size;
	min_malloc_size = min_malloc_size < SIZE_T_SIZE * 5 ? SIZE_T_SIZE * 5 : min_malloc_size;
	void *pos = linked_list_best_fit(new_size);
	/* a suitable free block found */
	if (pos != NULL) {
		return	place(pos,new_size,GET_SIZE(pos),true);
	}
	/* no free block found, ask for more memory */
	size_t size_to_alloc = new_size > CHUNK_SIZE ? new_size : CHUNK_SIZE;
	void *new_addr = mem_sbrk(size_to_alloc);
	if (new_addr == (void *) -1) return NULL;
	else {
		/* new_addr doesn't point to a initialized free block */
		/* so we shouldn't use place */
		/* place assume new_addr in the free block linked list */
		/* move the epilogue to the end of the heap */
		PUT(new_addr + size_to_alloc - SIZE_T_SIZE,PACK(0,1));
		return place(new_addr, new_size, size_to_alloc,false);
	}
}

/*
 * coalesce - Coalescing a free block with its neighbour blocks.
 */
void coalesce(void *ptr) {
	void *prev_ptr = PREV_BLK(ptr);
	void *next_ptr = NEXT_BLK(ptr);
	size_t total_size;
	/* both neighbour blocks are allocated, no coalescing */
	if (GET_ALLOC(prev_ptr) && GET_ALLOC(next_ptr)) {
		return;
	/* the previous block is free and the next block is allocated */
	} else if (!GET_ALLOC(prev_ptr) && GET_ALLOC(next_ptr)) {
		/* we must save total size in advance */
		/* GET_SIZE(ptr) + GET_SIZE(prev_ptr) will get changed as the prev block header changed */ 
		total_size = GET_SIZE(ptr) + GET_SIZE(prev_ptr);
		remove_from_linked_list(ptr);
		remove_from_linked_list(prev_ptr);
		PUT(HDRP(prev_ptr),PACK(total_size,0));
		PUT(FTRP(ptr),PACK(total_size,0));
		insert_to_linked_list_head(prev_ptr);
	/* the previous block is allocated and the next block is free */
	} else if (GET_ALLOC(prev_ptr) && !GET_ALLOC(next_ptr)) {
		total_size = GET_SIZE(ptr) + GET_SIZE(next_ptr);
		remove_from_linked_list(next_ptr);
		remove_from_linked_list(ptr);
		PUT(HDRP(ptr),PACK(total_size,0));
		PUT(FTRP(next_ptr),PACK(total_size,0));
		insert_to_linked_list_head(ptr);
	/* both the previous block and the next one is free */
	} else {
		total_size = GET_SIZE(next_ptr) + GET_SIZE(prev_ptr) + GET_SIZE(ptr);	
		remove_from_linked_list(ptr);
		remove_from_linked_list(prev_ptr);
		remove_from_linked_list(next_ptr);
		PUT(HDRP(prev_ptr),PACK(total_size,0));
		PUT(FTRP(next_ptr),PACK(total_size,0));
		insert_to_linked_list_head(prev_ptr);
	}
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
	/* changed the alloc bit to 0 and try coalescing with neighbours */
	PUT(HDRP(ptr),PACK(GET_SIZE(ptr),0));
	PUT(FTRP(ptr),PACK(GET_SIZE(ptr),0));
	insert_to_linked_list_head(ptr);
	coalesce(ptr);
}

/*
 * mm_memcpy - Copy size bytes from address src to address dst
 */
static void mm_memcpy(void *dst, void *src, size_t size) {
	if (src == dst) {
		return;
	/* avoid overlapping */
	} else if (src < dst) {
		for (int i = size - 1; i >= 0 ;i--) {
			*(char *)(dst + i) = *(char *)(src + i);
		}
	} else {
		for (int i = 0; i < size; i++) {
			*(char *)(dst + i) = *(char *)(src + i);
		}
	}
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
	if (ptr == NULL) {
		return mm_malloc(size);
	}
	if (size == 0) {
		mm_free(ptr);
		return ptr;
	}
	size_t new_size = ALIGN(size + 2 * SIZE_T_SIZE);
	if (new_size == GET_SIZE(ptr)) return ptr;
	if (new_size < GET_SIZE(ptr)) {
		return place(ptr, new_size, GET_SIZE(ptr), false);
	}
	/* the next block is free and coalesced size is large enough for request size */
	if (!GET_ALLOC(NEXT_BLK(ptr)) && (GET_SIZE(ptr) + GET_SIZE(NEXT_BLK(ptr))) >= new_size ) {
		/* just coalesce two blocks */
		size_t total_size = GET_SIZE(ptr) + GET_SIZE(NEXT_BLK(ptr));
		remove_from_linked_list(NEXT_BLK(ptr));
		cnt = cnt % 2 == 0 ? cnt : cnt + 1;
		return place(ptr, new_size, total_size, false);
	/* the previous block is free ... */
	} else if (!GET_ALLOC(PREV_BLK(ptr)) && (GET_SIZE(ptr) + GET_SIZE(PREV_BLK(ptr))) >= new_size) {
		/* call to PREV_BLK will get wrong result after mm_memcpy, so make a copy of it first */
		void* prev_ptr = PREV_BLK(ptr);
		size_t total_size = GET_SIZE(ptr) + GET_SIZE(prev_ptr);
		/* remove must be done before mm_memcpy */
		/* mm_memcpy will cover the data in the free block */
		remove_from_linked_list(PREV_BLK(ptr));
		/* start of the block now lies in previous block, so we have to copy data */
		if (total_size >= new_size + min_malloc_size && cnt % 2 != 0) {
			mm_memcpy(prev_ptr + total_size - new_size, ptr, GET_SIZE(ptr) - 2 * SIZE_T_SIZE);
		} else {
			mm_memcpy(prev_ptr, ptr, GET_SIZE(ptr) - 2 * SIZE_T_SIZE);		
		}
		/* coalesce blocks */
		return place(prev_ptr, new_size, total_size,false);
	/* similar */
	} else if (!GET_ALLOC(PREV_BLK(ptr)) && !GET_ALLOC(NEXT_BLK(ptr)) && (GET_SIZE(ptr) + GET_SIZE(PREV_BLK(ptr)) + GET_SIZE(NEXT_BLK(ptr)) >= new_size)) {
		void* prev_ptr = PREV_BLK(ptr);
		void* next_ptr = NEXT_BLK(ptr);
		size_t total_size = GET_SIZE(ptr) + GET_SIZE(prev_ptr) + GET_SIZE(next_ptr);
		remove_from_linked_list(PREV_BLK(ptr));
		remove_from_linked_list(NEXT_BLK(ptr));
		if (total_size >= new_size + min_malloc_size && cnt % 2 != 0) {
			mm_memcpy(prev_ptr + total_size - new_size, ptr, GET_SIZE(ptr) - 2 * SIZE_T_SIZE);
		} else {
			mm_memcpy(prev_ptr, ptr, GET_SIZE(ptr) - 2 * SIZE_T_SIZE);
		}
		return place(prev_ptr, new_size, total_size, false);
	}
	/*no coalescing is possible, asking for more memory */
	void* new_ptr = mm_malloc(size);
	if (new_ptr == NULL) return NULL;
	mm_memcpy(new_ptr, ptr, GET_SIZE(ptr) - 2 * SIZE_T_SIZE);
	/*free the old block */
	mm_free(ptr);
	return new_ptr;
}

