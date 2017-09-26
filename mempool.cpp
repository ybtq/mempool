#include "mempool.h"
#include <stdio.h>

/*//////////////////////////////////////////////////////////////////////////
Alignment macros
//////////////////////////////////////////////////////////////////////////*/
/* ALIGN() is only to be used to align on a power of 2 boundary */
#define ALIGN(size, boundary) \
    (((size) + ((boundary) - 1)) & ~((boundary) - 1))

/** Default alignment */
#define ALIGN_DEFAULT(size) ALIGN(size, 8)

#define SIZEOF_ALLOCATOR_T  ALIGN_DEFAULT(sizeof(allocator_t))
#define SIZEOF_MEMNODE_T	ALIGN_DEFAULT(sizeof(memnode_t))
#define SIZEOF_MEMPOOL_T	ALIGN_DEFAULT(sizeof(mempool_t))

#define ALLOCATOR_MAX_FREE_UNLIMITED 0


/*//////////////////////////////////////////////////////////////////////////
Global Variables
//////////////////////////////////////////////////////////////////////////*/
static bool			pools_initialized = false;
static mempool_t	*g_pool = NULL;
static allocator_t	*g_allocator = NULL;


bool allocator_create(allocator_t **allocator)
{
	allocator_t	*new_allocator;
	
	*allocator = NULL;
	if ((new_allocator = (allocator_t*)malloc(SIZEOF_ALLOCATOR_T)) == NULL) {
		return false;
	}
	
	memset(new_allocator, 0, SIZEOF_ALLOCATOR_T);
	new_allocator->max_free_index = ALLOCATOR_MAX_FREE_UNLIMITED;

	*allocator = new_allocator;

	return true;
}

void allocator_destroy(allocator_t *allocator)
{
	size_t		index;
	memnode_t	*node, **ref;
	
	for (index = 0; index < MAX_INDEX; index++)	{
		ref = &allocator->free[index];
		while ((node = *ref) != NULL) {
			*ref = node->next;
#ifdef ALLOCATOR_USES_MAP
			UnmapViewOfFile(node);
#else
			free(node);
#endif //ALLOCATOR_USES_MAP
		}
	}
	free(allocator);
}

memnode_t *allocator_alloc(allocator_t *allocator, size_t in_size)
{
	memnode_t	*node, **ref;
	size_t		max_index, size, i, index;

	/* Round up the block size to the next boundary, but always
     * allocate at least a certain size (MIN_ALLOC).
     */
	// 2013_07_23(Tue) : 因疏忽没有加SIZEOF_MEMNODE_T，导致各种内存溢出问题，汗颜哪。
	size = ALIGN(in_size + SIZEOF_MEMNODE_T, BOUNDARY_SIZE);
	if (size < in_size) {
		return NULL;
	}
	if (size < MIN_ALLOC) {
		size = MIN_ALLOC;
	}

	/* Find the index for this node size by
     * dividing its size by the boundary size
     */
	index = (size >> BOUNDARY_INDEX) - 1;
    
    if (index > 0xffffffffU) {
        return NULL;
    }

	if (index < allocator->max_index) {
#ifdef HAS_THREADS
		if (allocator->critical_section) {
			EnterCriticalSection(allocator->critical_section);
		}		
#endif //HAS_THREADS
		/* Walk the free list to see if there are
         * any nodes on it of the requested size
         *
         * NOTE: an optimization would be to check
         * allocator->free[index] first and if no
         * node is present, directly use
         * allocator->free[max_index].  This seems
         * like overkill though and could cause
         * memory waste.
         */
		max_index = allocator->max_index;
		ref = &allocator->free[index];
		i = index;
		while (*ref == NULL && i < max_index) {
			ref++;
			i++;
		}

		if ((node = *ref) != NULL) {
            /* If we have found a node and it doesn't have any
             * nodes waiting in line behind it _and_ we are on
             * the highest available index, find the new highest
             * available index
             */
            if ((*ref = node->next) == NULL && i >= max_index) {
                do {
                    ref--;
                    max_index--;
                }
                while (*ref == NULL && max_index > 0);

                allocator->max_index = max_index;
            }

            allocator->current_free_index += node->index + 1;
            if (allocator->current_free_index > allocator->max_free_index)
                allocator->current_free_index = allocator->max_free_index;
#ifdef HAS_THREADS
			if (allocator->critical_section) {
				LeaveCriticalSection(allocator->critical_section);
			}		
#endif //HAS_THREADS
			
            node->next = NULL;
            node->first_avail = (char *)node + SIZEOF_MEMNODE_T;

            return node;
        }
#ifdef HAS_THREADS
		if (allocator->critical_section) {
			LeaveCriticalSection(allocator->critical_section);
		}		
#endif //HAS_THREADS
	}
	/* If we found nothing, seek the sink (at index 0), if
     * it is not empty.
     */
	else if (allocator->free[0]) {
#ifdef HAS_THREADS
		if (allocator->critical_section) {
			EnterCriticalSection(allocator->critical_section);
		}		
#endif //HAS_THREADS

        /* Walk the free list to see if there are
         * any nodes on it of the requested size
         */
        ref = &allocator->free[0];
        while ((node = *ref) != NULL && index > node->index)
            ref = &node->next;

        if (node) {
            *ref = node->next;

            allocator->current_free_index += node->index + 1;
            if (allocator->current_free_index > allocator->max_free_index)
                allocator->current_free_index = allocator->max_free_index;

#ifdef HAS_THREADS
			if (allocator->critical_section) {
				LeaveCriticalSection(allocator->critical_section);
			}		
#endif //HAS_THREADS

            node->next = NULL;
            node->first_avail = (char *)node + SIZEOF_MEMNODE_T;

            return node;
        }
#ifdef HAS_THREADS
		if (allocator->critical_section) {
			LeaveCriticalSection(allocator->critical_section);
		}		
#endif //HAS_THREADS
    }

	/* If we haven't got a suitable node, malloc a new one
     * and initialize it.
     */
#ifdef ALLOCATOR_USES_MAP	
	HANDLE hMap = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, 
		PAGE_READWRITE, 0, size, NULL);
	if (hMap == NULL) {
		return NULL;
	}
	node = (memnode_t*)MapViewOfFile(hMap, FILE_MAP_READ | FILE_MAP_WRITE, 
		0, 0, size);
	CloseHandle(hMap);
	if (node == NULL || IsBadWritePtr(node, 1)) {
#else
	if ((node = (memnode_t*)malloc(size)) == NULL) {		
#endif	//ALLOCATOR_USES_MAP
		return NULL;
	}
	node->next = NULL;
    node->index = index;
    node->first_avail = (char *)node + SIZEOF_MEMNODE_T;
    node->endp = (char *)node + size;

    return node;
}

void allocator_free(allocator_t *allocator, memnode_t *node)
{
	memnode_t	*next, *freelist = NULL;
	size_t		index, max_index;
    size_t		max_free_index, current_free_index;

#ifdef HAS_THREADS
    if (allocator->critical_section) {
		EnterCriticalSection(allocator->critical_section);
	}		
#endif /* HAS_THREADS */

    max_index = allocator->max_index;
    max_free_index = allocator->max_free_index;
    current_free_index = allocator->current_free_index;

    /* Walk the list of submitted nodes and free them one by one,
     * shoving them in the right 'size' buckets as we go.
     */
    do {
        next = node->next;
        index = node->index;

        if (max_free_index != ALLOCATOR_MAX_FREE_UNLIMITED
            && index + 1 > current_free_index) {
            node->next = freelist;
            freelist = node;
        }
        else if (index < MAX_INDEX) {
            /* Add the node to the appropiate 'size' bucket.  Adjust
             * the max_index when appropiate.
             */
            if ((node->next = allocator->free[index]) == NULL
                && index > max_index) {
                max_index = index;
            }
            allocator->free[index] = node;
            if (current_free_index >= index + 1)
                current_free_index -= index + 1;
            else
                current_free_index = 0;
        }
        else {
            /* This node is too large to keep in a specific size bucket,
             * just add it to the sink (at index 0).
             */
            node->next = allocator->free[0];
            allocator->free[0] = node;
            if (current_free_index >= index + 1)
                current_free_index -= index + 1;
            else
                current_free_index = 0;
        }
    } while ((node = next) != NULL);

    allocator->max_index = max_index;
    allocator->current_free_index = current_free_index;

#ifdef HAS_THREADS
	if (allocator->critical_section) {
		LeaveCriticalSection(allocator->critical_section);
	}		
#endif //HAS_THREADS

    while (freelist != NULL) {
        node = freelist;
        freelist = node->next;
#ifdef ALLOCATOR_USES_MAP
		UnmapViewOfFile(node);
#else
		free(node);
#endif //ALLOCATOR_USES_MAP
    }
}


void allocator_max_free_set(allocator_t *allocator, size_t in_size)
{
	size_t	max_free_index;
	size_t	size = in_size;

#if HAS_THREADS
    if (allocator->critical_section) {
		EnterCriticalSection(allocator->critical_section);
	}
#endif /* HAS_THREADS */

    max_free_index = ALIGN(size, BOUNDARY_SIZE) >> BOUNDARY_INDEX;
    allocator->current_free_index += max_free_index;
    allocator->current_free_index -= allocator->max_free_index;
    allocator->max_free_index = max_free_index;
    if (allocator->current_free_index > max_free_index)
        allocator->current_free_index = max_free_index;

#if HAS_THREADS
	if (allocator->critical_section) {
		LeaveCriticalSection(allocator->critical_section);
	}		
#endif  /* HAS_THREADS */
}



bool mempool_create(mempool_t **newpool, mempool_t *parent, allocator_t *allocator)
{
	mempool_t	*pool;
	memnode_t	*node;

	*newpool = NULL;
	if (!parent) {
		parent = g_pool;
	}

	/* parent will always be non-NULL here except the first time a
     * pool is created, in which case allocator is guaranteed to be
     * non-NULL. */
	if (allocator == NULL) {
		allocator = parent->allocator;
	}

	if ((node = allocator_alloc(allocator, 
		MIN_ALLOC - SIZEOF_MEMNODE_T)) == NULL) {
		return false;
	}

	node->next = node;
    node->ref = &node->next;

    pool = (mempool_t *)node->first_avail;
    node->first_avail = pool->self_first_avail = (char *)pool + SIZEOF_MEMPOOL_T;
	
	pool->allocator = allocator;
	pool->active = pool->self = node;
	pool->child = NULL;
	pool->parent = NULL;
    pool->sibling = NULL;
    pool->ref = NULL;
	
    if ((pool->parent = parent) != NULL) {
#ifdef HAS_THREADS
		LPCRITICAL_SECTION	critical_section;
		if ((critical_section = parent->allocator->critical_section) != NULL) {
			EnterCriticalSection(critical_section);
		}
#endif /* HAS_THREADS */

        if ((pool->sibling = parent->child) != NULL)
            pool->sibling->ref = &pool->sibling;

        parent->child = pool;
        pool->ref = &parent->child;

#ifdef HAS_THREADS
        if (critical_section)
            LeaveCriticalSection(critical_section);
#endif /* HAS_THREADS */
    }
    else {
        pool->sibling = NULL;
        pool->ref = NULL;
    }
	
	*newpool = pool;
	return true;
}


bool mempool_create_unmanaged(mempool_t **newpool, allocator_t *allocator)
{
	mempool_t	*pool;
	memnode_t	*node;
	allocator_t	*pool_allocator;

	*newpool = NULL;

	if ((pool_allocator = allocator) == NULL) {
		if (!allocator_create(&pool_allocator)) {
			return false;
		}
	}
	if ((node = allocator_alloc(pool_allocator, 
		MIN_ALLOC - SIZEOF_MEMNODE_T)) == NULL) {
		return false;
	}

	node->next = node;
    node->ref = &node->next;

    pool = (mempool_t *)node->first_avail;
    node->first_avail = pool->self_first_avail = (char *)pool + SIZEOF_MEMPOOL_T;
	
	pool->allocator = pool_allocator;
	pool->active = pool->self = node;
	pool->child = NULL;
	pool->parent = NULL;
    pool->sibling = NULL;
    pool->ref = NULL;
	
	if (!allocator) {
		pool_allocator->owner = pool;
	}
	*newpool = pool;

	return true;
}


void mempool_clear(mempool_t *pool)
{
	memnode_t	*active;

	/* Destroy the subpools.  The subpools will detach themselves from
     * this pool thus this loop is safe and easy.
     */
	while (pool->child) {
		mempool_destroy(pool->child);
	}

	/* Find the node attached to the pool structure, reset it, make
     * it the active node and free the rest of the nodes.
     */
    active = pool->active = pool->self;
    active->first_avail = pool->self_first_avail;

    if (active->next == active)
        return;

    *active->ref = NULL;
    allocator_free(pool->allocator, active->next);
    active->next = active;
    active->ref = &active->next;
}


void mempool_destroy(mempool_t *pool)
{
	memnode_t	*active;
    allocator_t	*allocator;

	/* Destroy the subpools.  The subpools will detach themselves from
     * this pool thus this loop is safe and easy.
     */
	while (pool->child) {
		mempool_destroy(pool->child);
	}

	/* Remove the pool from the parents child list */
    if (pool->parent) {
#ifdef HAS_THREADS
        LPCRITICAL_SECTION critical_section;

        if ((critical_section = pool->parent->allocator->critical_section) != NULL)
            EnterCriticalSection(critical_section);
#endif /* HAS_THREADS */

        if ((*pool->ref = pool->sibling) != NULL)
            pool->sibling->ref = pool->ref;

#ifdef HAS_THREADS
        if (critical_section)
            LeaveCriticalSection(critical_section);
#endif /* HAS_THREADS */
    }
	
    /* Find the block attached to the pool structure.  Save a copy of the
     * allocator pointer, because the pool struct soon will be no more.
     */
    allocator = pool->allocator;
    active = pool->self;
    *active->ref = NULL;

#ifdef HAS_THREADS
    if (allocator->owner == pool) {
        /* Make sure to remove the lock, since it is highly likely to
         * be invalid now.
         */
        allocator->critical_section = NULL;
    }
#endif /* HAS_THREADS */

    /* Free all the nodes in the pool (including the node holding the
     * pool struct), by giving them back to the allocator.
     */
    allocator_free(allocator, active);

    /* If this pool happens to be the owner of the allocator, free
     * everything in the allocator (that includes the pool struct
     * and the allocator).  Don't worry about destroying the optional mutex
     * in the allocator, it will have been destroyed by the cleanup function.
     */
    if (allocator->owner == pool) {
        allocator_destroy(allocator);
    }
}


/* Node list management helper macros; list_insert() inserts 'node'
 * before 'point'. */
#define list_insert(node, point) do {           \
    node->ref = point->ref;                     \
    *node->ref = node;                          \
    node->next = point;                         \
    point->ref = &node->next;                   \
} while (0)

/* list_remove() removes 'node' from its list. */
#define list_remove(node) do {                  \
    *node->ref = node->next;                    \
    node->next->ref = node->ref;                \
} while (0)

/* Returns the amount of free space in the given node. */
#define node_free_space(node_) ((size_t)(node_->endp - node_->first_avail))

void *mempool_alloc(mempool_t *pool, size_t in_size)
{
	memnode_t *active, *node;
    void *mem;
    size_t size, free_index;

    size = ALIGN_DEFAULT(in_size);
    if (size < in_size) {
        return NULL;
    }
    active = pool->active;
	
    /* If the active node has enough bytes left, use it. */
    if (size <= node_free_space(active)) {
        mem = active->first_avail;
        active->first_avail += size;

        return mem;
    }

    node = active->next;
    if (size <= node_free_space(node)) {
        list_remove(node);
    }
    else if ((node = allocator_alloc(pool->allocator, size)) == NULL) {
		return NULL;       
    }

    node->free_index = 0;

    mem = node->first_avail;
    node->first_avail += size;

    list_insert(node, active);

    pool->active = node;

    free_index = (ALIGN(active->endp - active->first_avail + 1,
                            BOUNDARY_SIZE) - BOUNDARY_SIZE) >> BOUNDARY_INDEX;

    active->free_index = free_index;
    node = active->next;
    if (free_index >= node->free_index)
        return mem;
    do {
        node = node->next;
    }
    while (free_index < node->free_index);

    list_remove(active);
    list_insert(active, node);

    return mem;
}

void *mempool_calloc(mempool_t *pool, size_t in_size)
{
	void *mem;

	mem = mempool_alloc(pool, in_size);
	if (mem != NULL) {
		memset(mem, 0, in_size);
	}

	return mem;
}

bool pool_initialize()
{
	if (pools_initialized) {
		return true;
	}
	if (!allocator_create(&g_allocator)) {
		return false;
	}
	
	g_pool = NULL;
	if (!mempool_create(&g_pool, NULL, g_allocator)) {
		allocator_destroy(g_allocator);
		g_allocator = NULL;
		pools_initialized = false;
		return false;
	}

	g_allocator->max_free_index = 100;
	
#ifdef HAS_THREADS
	LPCRITICAL_SECTION	critical_section = 
		(LPCRITICAL_SECTION)mempool_alloc(g_pool, sizeof(CRITICAL_SECTION));
	InitializeCriticalSection(critical_section);
	g_allocator->critical_section = critical_section;	
#endif //HAS_THREADS
	g_allocator->owner = g_pool;

	return true;
}


void pool_terminate(void)
{
	if (!pools_initialized) {
		return;
	}
#ifdef HAS_THREADS
	if (g_allocator->critical_section) {
		DeleteCriticalSection (g_allocator->critical_section);
		g_allocator->critical_section = NULL;
	}	
#endif //HAS_THREADS
	mempool_destroy(g_pool);
	g_pool = NULL;
	g_allocator = NULL;
}