#include <stdio.h>
#include <assert.h>
#include "mempool.h"

int main()
{
    pool_initialize();
    mempool_t *pool;
    mempool_create(&pool, NULL, NULL);
    char *buf = (char*)mempool_alloc(pool, 32);
    assert(buf);
    printf("alloc a buf success.\n");
    mempool_destroy(pool);
    pool_terminate();
    return 0;
}