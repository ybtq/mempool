## 初衷

这个内存池代码是仿apr内存池而写。是在安天任职时，开发Ring3级反APT（APT的意思是高级可持续威胁）系统时，由于分析病毒的dll需要往外部发送大量监控数据（频繁申请释放内存）。而仅仅只用apr的内存池，却包含整个apr的库，有点得不偿失。另外这个dll需要用QueueUserAPC或CreateRemoteThread方式注入到样本进程中，且当时apr的静态库版本编译有两个错误无法解决，使用apr的动态库，则也需要将其同时注入，这显然是不合理的。因此决定仿写一个内存池。

## 使用方式

```
include <stdio.h>
include "mempool.h"

int main()
{
	mempool_t *pool = NULL;
	mempool_create(&pool, NULL, NULL);
	char *szName = (char*)mempool_alloc(pool, 32);
	assert(szName);
	mempool_destroy(pool);
	return 0;
}
```