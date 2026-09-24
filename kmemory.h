#ifndef KIRILLOS_MEMORY_H
#define KIRILLOS_MEMORY_H

#include <stddef.h>

void kmemory_init(void);
void* kmalloc(size_t size);
void kfree(void* pointer);
size_t kmemory_used(void);
size_t kmemory_capacity(void);

#endif
