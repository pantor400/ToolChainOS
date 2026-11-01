#ifndef HEAP_H
#define HEAP_H
#include <stdint.h>
#include <stddef.h>
typedef struct {uint64_t pages,arenas,blocks,used,capacity,invalid_frees,failures;} heap_info_t;
void *kmalloc(size_t size);
void *kcalloc(size_t count,size_t size);
void kfree(void *ptr);
heap_info_t heap_info(void);
int heap_check(void);
void heap_stats(void);
void heap_test(void);
#endif
