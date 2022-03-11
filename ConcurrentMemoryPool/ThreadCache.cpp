#include"ThreadCache.h"
#include"CentralCache.h"

void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	size_t allocNum = SizeClass::RoundUp(size);
	size_t index = SizeClass::Index(allocNum);
	//如果对应的自由链表桶不为空，直接从桶中取出内存块
	//如果为空，则从central_cache中获取内存块
	if (!_freeList[index].IsEmpty())
	{
		return _freeList[index].Pop();
	}
	else
	{
		return FetchFromCentralCache(index, allocNum);
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	//找到映射的自由链表桶，将对象插入
	size_t index = SizeClass::Index(size);
	_freeList[index].Push(ptr);

	//当链表长度大于一次所能批量申请的内存块数目时，就释放一段给centralCache
	if (_freeList[index].Size() >= _freeList[index].MaxSize())
	{
		ListTooLong(_freeList[index], size);
	}

}

void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	//采用慢开始调节算法
	size_t batchNum = std::min(_freeList[index].MaxSize(), SizeClass::NumMovSize(size));
	if (batchNum == _freeList[index].MaxSize())
	{
		_freeList[index].MaxSize() ++;
	}

	void* start = nullptr;
	void* end = nullptr;

	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);

	if (actualNum == 1)
	{
		assert(start == end && start != nullptr);
		return start;
	}
	else
	{
		_freeList[index].PushRange(Nextobj(start), end, actualNum - 1);
		return start;
	}

	return nullptr;
}

void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize());
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}