## 背景

这个内存池代码是仿apr内存池而写。是在安天任职时，开发Ring3级反APT（APT的意思是高级可持续威胁）系统，由于分析病毒的动态库需要往外部发送大量监控数据（频繁申请释放内存）。而仅仅只用apr的内存池，却包含整个apr的库，有点得不偿失。另外这个dll需要用QueueUserAPC或CreateRemoteThread方式注入到样本进程中，且当时apr的静态库版本编译有两个错误无法解决，使用apr的动态库，则也需要将其同时注入，这显然是不合理的。因此决定仿写一个内存池。

## 说明

* HAS_THREADS: 多线程分配内存时，需要使用本宏以保证线程安全。
* ALLOCATOR_USES_MAP: 是否内存池分配使用文件映射。

参见`pool_test.cpp`示例代码：
```
#include <assert.h>
#include "mempool.h"

int main()
{
    pool_initialize();
    mempool_t *pool;
    mempool_create(&pool, NULL, NULL);
    char *buf = (char*)mempool_alloc(pool, 32);
    assert(buf);
    mempool_destroy(pool);
    pool_terminate();
    return 0;
}
```

**Linux**

```
g++ pool_test.cpp mempool.h mempool.cpp -o pool_test -DHAS_THREADS -DALLOCATOR_USES_MAP
```

**Windows**

使用pool_test.vcxproj编译。