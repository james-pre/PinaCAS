#ifndef COMPILE_PC

#include "heap.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ti/vars.h>

/*Free RAM left to the OS for the variables the program creates*/
#define RAM_MARGIN 16384
/*Blocks up to this size, including the header, are kept in bins by exact size*/
#define SMALL_LIMIT 128
#define GRANULARITY 4
#define BINS (SMALL_LIMIT / GRANULARITY + 1)
#define HEADER sizeof(size_t)

typedef struct free_block {
	size_t size;
	struct free_block *next;
} free_block_t;

typedef struct {
	uint8_t *next, *end;
} region_t;

extern uint8_t __heap_low[], __heap_high[];

void *ram_Reserve(size_t size);

static region_t regions[2] = {{__heap_low, __heap_high}};
static unsigned region_count = 1;
static free_block_t *bins[BINS];
static free_block_t *large;
static void (*on_failure)(void);

void heap_Init(void) {
	const size_t free_ram = os_MemChk(NULL);

	if (region_count > 1 || free_ram <= RAM_MARGIN + SMALL_LIMIT)
		return;

	uint8_t *block = ram_Reserve(free_ram - RAM_MARGIN);
	regions[region_count].next = block;
	regions[region_count].end = block + free_ram - RAM_MARGIN;
	region_count++;
}

void heap_SetFailure(void (*failure)(void)) {
	on_failure = failure;
}

static size_t block_size(size_t size) {
	size_t total = size + HEADER;

	if (total < sizeof(free_block_t))
		total = sizeof(free_block_t);

	return (total + GRANULARITY - 1) & ~(size_t)(GRANULARITY - 1);
}

static void release(free_block_t *block) {
	free_block_t **list = block->size <= SMALL_LIMIT ? &bins[block->size / GRANULARITY] : &large;

	block->next = *list;
	*list = block;
}

/*Takes a block of at least total bytes from the large list, splitting off the rest*/
static free_block_t *take_large(size_t total) {
	for (free_block_t **link = &large; *link != NULL; link = &(*link)->next) {
		free_block_t *block = *link;

		if (block->size < total)
			continue;

		*link = block->next;

		if (block->size - total >= sizeof(free_block_t)) {
			free_block_t *rest = (free_block_t *)((uint8_t *)block + total);
			rest->size = block->size - total;
			block->size = total;
			release(rest);
		}

		return block;
	}

	return NULL;
}

static free_block_t *bump(size_t total) {
	for (unsigned i = 0; i < region_count; i++) {
		if ((size_t)(regions[i].end - regions[i].next) >= total) {
			free_block_t *block = (free_block_t *)regions[i].next;
			regions[i].next += total;
			block->size = total;
			return block;
		}
	}

	return NULL;
}

/*Merges two lists sorted by address*/
static free_block_t *merge(free_block_t *a, free_block_t *b) {
	free_block_t head, *tail = &head;

	while (a != NULL && b != NULL) {
		if (a < b) {
			tail->next = a;
			a = a->next;
		} else {
			tail->next = b;
			b = b->next;
		}
		tail = tail->next;
	}

	tail->next = a != NULL ? a : b;
	return head.next;
}

static free_block_t *sort(free_block_t *list) {
	if (list == NULL || list->next == NULL)
		return list;

	free_block_t *slow = list;
	free_block_t *fast = list->next;
	while (fast != NULL && fast->next != NULL) {
		slow = slow->next;
		fast = fast->next->next;
	}

	free_block_t *second = slow->next;
	slow->next = NULL;

	return merge(sort(list), sort(second));
}

/*Joins adjacent free blocks, returns those at the end of a region to it, and refills the lists*/
static void coalesce(void) {
	free_block_t *all = large;

	for (unsigned i = 0; i < BINS; i++) {
		free_block_t *block;
		while ((block = bins[i]) != NULL) {
			bins[i] = block->next;
			block->next = all;
			all = block;
		}
	}

	large = NULL;
	all = sort(all);

	for (free_block_t *block = all, *next; block != NULL; block = next) {
		while (block->next != NULL && (uint8_t *)block + block->size == (uint8_t *)block->next) {
			block->size += block->next->size;
			block->next = block->next->next;
		}

		next = block->next;

		unsigned i;
		for (i = 0; i < region_count; i++) {
			if ((uint8_t *)block + block->size == regions[i].next) {
				regions[i].next = (uint8_t *)block;
				break;
			}
		}

		if (i == region_count)
			release(block);
	}
}

static free_block_t *allocate(size_t total) {
	free_block_t *block = NULL;

	if (total <= SMALL_LIMIT && (block = bins[total / GRANULARITY]) != NULL) {
		bins[total / GRANULARITY] = block->next;
		return block;
	}

	if (total > SMALL_LIMIT && (block = take_large(total)) != NULL)
		return block;

	if ((block = bump(total)) != NULL)
		return block;

	return take_large(total);
}

void *malloc(size_t size) {
	const size_t total = block_size(size);
	free_block_t *block = allocate(total);

	if (block == NULL) {
		coalesce();
		block = allocate(total);
	}

	if (block == NULL) {
		if (on_failure != NULL)
			on_failure();
		return NULL;
	}

	return (uint8_t *)block + HEADER;
}

void free(void *pointer) {
	if (pointer != NULL)
		release((free_block_t *)((uint8_t *)pointer - HEADER));
}

void *realloc(void *pointer, size_t size) {
	if (pointer == NULL)
		return malloc(size);

	free_block_t *block = (free_block_t *)((uint8_t *)pointer - HEADER);

	if (block->size >= block_size(size))
		return pointer;

	void *moved = malloc(size);
	if (moved != NULL) {
		memcpy(moved, pointer, block->size - HEADER);
		free(pointer);
	}

	return moved;
}

size_t heap_Available(void) {
	size_t available = 0;

	for (unsigned i = 0; i < region_count; i++)
		available += regions[i].end - regions[i].next;

	for (unsigned i = 0; i < BINS; i++) {
		for (const free_block_t *block = bins[i]; block != NULL; block = block->next)
			available += block->size;
	}

	for (const free_block_t *block = large; block != NULL; block = block->next)
		available += block->size;

	return available;
}

#else
typedef int make_iso_compilers_happy;
#endif
