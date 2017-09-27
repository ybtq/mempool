/*//////////////////////////////////////////////////////////////////////////
Copyright (c) 2012 ~ 2013, AntiyLabs
Author : ybt
Date   : 2013-06-25(Tue)

注：本内存池非原创，是apr内存池的简化版。

//////////////////////////////////////////////////////////////////////////*/
#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_

#include <stdlib.h>

struct allocator_t;
struct memnode_t;
struct mempool_t;

bool        allocator_create(allocator_t **mem_allocator);
void        allocator_destroy(allocator_t *mem_allocator);
memnode_t   *allocator_alloc(allocator_t *mem_allocator, size_t in_size);
void        allocator_free(allocator_t *mem_allocator, memnode_t *node);

void        allocator_max_free_set(allocator_t *mem_allocator, size_t in_size);

bool        mempool_create(mempool_t **newpool, mempool_t *parent, allocator_t *mem_allocator);
bool        mempool_create_unmanaged(mempool_t **newpool, allocator_t *mem_allocator);
void        mempool_clear(mempool_t *pool);
void        mempool_destroy(mempool_t *pool);

void        *mempool_alloc(mempool_t *pool, size_t in_size);
void        *mempool_calloc(mempool_t *pool, size_t in_size);

bool        pool_initialize(void);
void        pool_terminate(void);

#endif //_MEMPOOL_H_