/*//////////////////////////////////////////////////////////////////////////
Copyright (c) 2012 ~ 2013, AntiyLabs
Author : ybt
Date   : 2013-06-25(Tue)

注：本内存池非原创，是apr内存池的简化版，并且只适用于Windows。
(因apr内存池在windows系统使用有点问题)

//////////////////////////////////////////////////////////////////////////*/
#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_

#include <stdlib.h>
#include <windows.h>

#define MIN_ALLOC (2 * BOUNDARY_SIZE)
#define MAX_INDEX   20

#define BOUNDARY_INDEX 12
#define BOUNDARY_SIZE (1 << BOUNDARY_INDEX)

typedef struct memnode_t {
    struct memnode_t	*next;			/**< next memnode */
    struct memnode_t	**ref;			/**< reference to self */
    unsigned int		index;			/**< size */
    unsigned int		free_index;		/**< how much free */
    char		*first_avail;	/**< pointer to first free memory */
    char		*endp;			/**< pointer to end of free memory */
} memnode_t;

typedef struct mempool_t {
	struct mempool_t	*parent;
    struct mempool_t	*child;
    struct mempool_t	*sibling;
    struct mempool_t	**ref;
	struct allocator_t	*allocator;

	struct memnode_t	*active;
	struct memnode_t	*self;			/* The node containing the pool itself */
	char		*self_first_avail;

#ifdef HAS_THREADS
	LPCRITICAL_SECTION	critical_section;
#endif //HAS_THREADS
} mempool_t;

typedef struct allocator_t {
	/** largest used index into free[], always < MAX_INDEX */
	unsigned int	max_index;
	/** Total size (in BOUNDARY_SIZE multiples) of unused memory before
     * blocks are given back. @see apr_allocator_max_free_set().
     * @note Initialized to APR_ALLOCATOR_MAX_FREE_UNLIMITED,
     * which means to never give back blocks.
     */
	unsigned int	max_free_index;
	/**
     * Memory size (in BOUNDARY_SIZE multiples) that currently must be freed
     * before blocks are given back. Range: 0..max_free_index
     */
	unsigned int	current_free_index;
#ifdef HAS_THREADS
	LPCRITICAL_SECTION	critical_section;
#endif //HAS_THREADS
	struct mempool_t	*owner;
	/**
     * Lists of free nodes. Slot 0 is used for oversized nodes,
     * and the slots 1..MAX_INDEX-1 contain nodes of sizes
     * (i+1) * BOUNDARY_SIZE. Example for BOUNDARY_INDEX == 12:
     * slot  0: nodes larger than 81920
     * slot  1: size  8192
     * slot  2: size 12288
     * ...
     * slot 19: size 81920
     */
	struct memnode_t	*free[MAX_INDEX];
} allocator_t;


bool		allocator_create(allocator_t **mem_allocator);
void		allocator_destroy(allocator_t *mem_allocator);
memnode_t	*allocator_alloc(allocator_t *mem_allocator, size_t in_size);
void		allocator_free(allocator_t *mem_allocator, memnode_t *node);

void		allocator_max_free_set(allocator_t *mem_allocator, size_t in_size);

bool		mempool_create(mempool_t **newpool, mempool_t *parent, allocator_t *mem_allocator);
bool		mempool_create_unmanaged(mempool_t **newpool, allocator_t *mem_allocator);
void		mempool_clear(mempool_t *pool);
void		mempool_destroy(mempool_t *pool);

void		*mempool_alloc(mempool_t *pool, size_t in_size);
void		*mempool_calloc(mempool_t *pool, size_t in_size);

bool		pool_initialize(void);
void		pool_terminate(void);

#endif //_MEMPOOL_H_