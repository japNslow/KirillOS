#include "kmemory.h"

#define KHEAP_SIZE 65536
#define KHEAP_ALIGNMENT 8

static unsigned char heap[KHEAP_SIZE];
static size_t heap_offset;

static size_t align_up(size_t value) {
    return (value + KHEAP_ALIGNMENT - 1) &
           ~(KHEAP_ALIGNMENT - 1);
}

void kmemory_init(void) {
    heap_offset = 0;
}

void* kmalloc(size_t size) {
    size_t aligned_size;
    size_t next_offset;

    if (size == 0) return 0;
    aligned_size = align_up(size);
    next_offset = heap_offset + aligned_size;
    if (next_offset < heap_offset || next_offset > KHEAP_SIZE) return 0;

    void* result = &heap[heap_offset];
    heap_offset = next_offset;
    return result;
}

void kfree(void* pointer) {
    (void)pointer;
}

size_t kmemory_used(void) {
    return heap_offset;
}

size_t kmemory_capacity(void) {
    return KHEAP_SIZE;
}
