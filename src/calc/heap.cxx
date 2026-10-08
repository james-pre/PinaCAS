#ifndef COMPILE_PC

#include "heap.hxx"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ti/vars.h>

/*Free RAM left to the OS for the variables the program creates*/
constexpr size_t RAM_MARGIN = 16384;
/*Blocks up to this size, including the header, are kept in bins by exact size*/
constexpr size_t SMALL_LIMIT = 128;
constexpr size_t GRANULARITY = 4;
constexpr size_t BINS = SMALL_LIMIT / GRANULARITY + 1;
constexpr size_t HEADER = sizeof(size_t);

struct FreeBlock {
	size_t size;
	FreeBlock *next;
};

struct Region {
	uint8_t *next, *end;
};

extern "C" {
extern uint8_t __heap_low[], __heap_high[];

void *ram_Reserve(size_t size);
}

static Region regions[2] = {{__heap_low, __heap_high}};
static unsigned region_count = 1;
static FreeBlock *bins[BINS];
static FreeBlock *large;
static void (*on_failure)(void);

void heap::init() {
	const size_t free_ram = os_MemChk(nullptr);

	if (region_count > 1 || free_ram <= RAM_MARGIN + SMALL_LIMIT)
		return;

	uint8_t *block = static_cast<uint8_t *>(ram_Reserve(free_ram - RAM_MARGIN));
	regions[region_count].next = block;
	regions[region_count].end = block + free_ram - RAM_MARGIN;
	region_count++;
}

void heap::setFailure(void (*failure)()) {
	on_failure = failure;
}

static size_t block_size(size_t size) {
	size_t total = size + HEADER;

	if (total < sizeof(FreeBlock))
		total = sizeof(FreeBlock);

	return (total + GRANULARITY - 1) & ~(GRANULARITY - 1);
}

static void release(FreeBlock *block) {
	FreeBlock **list = block->size <= SMALL_LIMIT ? &bins[block->size / GRANULARITY] : &large;

	block->next = *list;
	*list = block;
}

/*Takes a block of at least total bytes from the large list, splitting off the rest*/
static FreeBlock *take_large(size_t total) {
	for (FreeBlock **link = &large; *link != nullptr; link = &(*link)->next) {
		FreeBlock *block = *link;

		if (block->size < total)
			continue;

		*link = block->next;

		if (block->size - total >= sizeof(FreeBlock)) {
			FreeBlock *rest = reinterpret_cast<FreeBlock *>((reinterpret_cast<uint8_t *>(block) + total));
			rest->size = block->size - total;
			block->size = total;
			release(rest);
		}

		return block;
	}

	return nullptr;
}

static FreeBlock *bump(size_t total) {
	for (unsigned i = 0; i < region_count; i++) {
		if (static_cast<size_t>((regions[i].end - regions[i].next)) >= total) {
			FreeBlock *block = reinterpret_cast<FreeBlock *>(regions[i].next);
			regions[i].next += total;
			block->size = total;
			return block;
		}
	}

	return nullptr;
}

/*Merges two lists sorted by address*/
static FreeBlock *merge(FreeBlock *a, FreeBlock *b) {
	FreeBlock head, *tail = &head;

	while (a != nullptr && b != nullptr) {
		if (a < b) {
			tail->next = a;
			a = a->next;
		} else {
			tail->next = b;
			b = b->next;
		}
		tail = tail->next;
	}

	tail->next = a != nullptr ? a : b;
	return head.next;
}

static FreeBlock *sort(FreeBlock *list) {
	if (list == nullptr || list->next == nullptr)
		return list;

	FreeBlock *slow = list;
	FreeBlock *fast = list->next;
	while (fast != nullptr && fast->next != nullptr) {
		slow = slow->next;
		fast = fast->next->next;
	}

	FreeBlock *second = slow->next;
	slow->next = nullptr;

	return merge(sort(list), sort(second));
}

/*Joins adjacent free blocks, returns those at the end of a region to it, and refills the lists*/
static void coalesce(void) {
	FreeBlock *all = large;

	for (unsigned i = 0; i < BINS; i++) {
		FreeBlock *block;
		while ((block = bins[i]) != nullptr) {
			bins[i] = block->next;
			block->next = all;
			all = block;
		}
	}

	large = nullptr;
	all = sort(all);

	for (FreeBlock *block = all, *next; block != nullptr; block = next) {
		while (block->next != nullptr &&
			   reinterpret_cast<uint8_t *>(block) + block->size == reinterpret_cast<uint8_t *>(block->next)) {
			block->size += block->next->size;
			block->next = block->next->next;
		}

		next = block->next;

		unsigned i;
		for (i = 0; i < region_count; i++) {
			if (reinterpret_cast<uint8_t *>(block) + block->size == regions[i].next) {
				regions[i].next = reinterpret_cast<uint8_t *>(block);
				break;
			}
		}

		if (i == region_count)
			release(block);
	}
}

static FreeBlock *allocate(size_t total) {
	FreeBlock *block = nullptr;

	if (total <= SMALL_LIMIT && (block = bins[total / GRANULARITY]) != nullptr) {
		bins[total / GRANULARITY] = block->next;
		return block;
	}

	if (total > SMALL_LIMIT && (block = take_large(total)) != nullptr)
		return block;

	if ((block = bump(total)) != nullptr)
		return block;

	return take_large(total);
}

void *malloc(size_t size) {
	const size_t total = block_size(size);
	FreeBlock *block = allocate(total);

	if (block == nullptr) {
		coalesce();
		block = allocate(total);
	}

	if (block == nullptr) {
		if (on_failure != nullptr)
			on_failure();
		return nullptr;
	}

	return reinterpret_cast<uint8_t *>(block) + HEADER;
}

void free(void *pointer) {
	if (pointer != nullptr)
		release(reinterpret_cast<FreeBlock *>((reinterpret_cast<uint8_t *>(pointer) - HEADER)));
}

void *realloc(void *pointer, size_t size) {
	if (pointer == nullptr)
		return malloc(size);

	FreeBlock *block = reinterpret_cast<FreeBlock *>((reinterpret_cast<uint8_t *>(pointer) - HEADER));

	if (block->size >= block_size(size))
		return pointer;

	void *moved = malloc(size);
	if (moved != nullptr) {
		memcpy(moved, pointer, block->size - HEADER);
		free(pointer);
	}

	return moved;
}

size_t heap::available() {
	size_t available = 0;

	for (unsigned i = 0; i < region_count; i++)
		available += static_cast<size_t>(regions[i].end - regions[i].next);

	for (unsigned i = 0; i < BINS; i++) {
		for (const FreeBlock *block = bins[i]; block != nullptr; block = block->next)
			available += block->size;
	}

	for (const FreeBlock *block = large; block != nullptr; block = block->next)
		available += block->size;

	return available;
}

#endif
