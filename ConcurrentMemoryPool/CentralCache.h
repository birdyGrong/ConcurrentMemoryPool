#pragma once
#include"Common.h"

//全局只有一个CentralCache，因此可以使用单例模式
class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}

	//从中心缓存获取一定数量的对象给threadCache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	//从SpanList或者PageCache中获取一个span
	Span* GetOneSpan(SpanList& spanList, size_t size);

	//将一定数量的内存块释放到Span中
	void ReleaseListToSpans(void* start, size_t size);

private:
	CentralCache() = default;
	CentralCache& operator=(const CentralCache&) = delete;
	CentralCache(const CentralCache&) = delete;
	static CentralCache _sInst;
private:
	SpanList _spanList[NFREELISTS];
};