#include"PageCache.h"
PageCache PageCache::_cInst;


Span* PageCache::MapObjectToSpan(void* obj)
{
	PageID id = (PageID)obj >> PAGE_SHIFT;
	//std::unique_lock<std::mutex> lock(_mtx);
	/*auto ret = _idSpanMap.find(id);
	if (ret != nullptr)
	{
		return ret;
	}
	else
	{
		assert(false);
		return nullptr;
	}*/
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
}

Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	//大于128 page的直接向堆申请
	if (k > NPAGES - 1)
	{
		void* ptr = SystemAlloc(k);
		Span* span = _spanPool.New();
		span->_n = k;
		span->_pageId = ((PageID)ptr >> PAGE_SHIFT);
		span->_objSize = k >> PAGE_SHIFT;
		//_idSpanMap[span->_pageId] = span;
		_idSpanMap.Set(span->_pageId, span);

		return span;
	}

	if (!_spanList[k].IsEmpty())
	{
		Span* span = _spanList[k].PopFront();
		for (PageID i = 0; i < span->_n; i++)
		{
			//_idSpanMap[span->_pageId + i] = span;
			_idSpanMap.Set(span->_pageId + i, span);
		}
		return span;
	}

	for (int i = k + 1; i < NPAGES; i++)
	{
		if (!_spanList[i].IsEmpty())
		{
			Span* nSpan = _spanList[i].PopFront();
			Span* kSpan = _spanPool.New();
			kSpan->_n = k;
			kSpan->_pageId = nSpan->_pageId;

			nSpan->_n -= k;
			nSpan->_pageId += k;

			_spanList[nSpan->_n].PushFront(nSpan);

			//存储nSpan的首尾页号与nspan的映射，方便后续合并时的查找
			//_idSpanMap[nSpan->_pageId] = nSpan;
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.Set(nSpan->_pageId, nSpan);
			_idSpanMap.Set(nSpan->_pageId + nSpan->_n - 1, nSpan);

			//将kSpand中每一个页的页号与kSpan进行映射
			for (PageID i = 0; i < kSpan->_n; i++)
			{
				//_idSpanMap[kSpan->_pageId + i] = kSpan;
				_idSpanMap.Set(kSpan->_pageId + i, kSpan);
			}


			return kSpan;
		}
	}
	//如果执行到这里，说明目前pagecache中也没有span，需要跟内核申请
	//先申请一个NPAGES大小的span，挂到NPAGES，再进行切分

	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PageID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;
	_spanList[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);

}

void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//大于128页的page直接还给堆
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		_spanPool.Delete(span);
		return;
	}
	//对前后的页进行合并
	while (true)
	{
		PageID prevId = span->_pageId - 1;
		//auto ret = _idSpanMap.find(prevId);
		auto ret = (Span*)_idSpanMap.get(prevId);
		//前面的页号没有了，不合并了
		//这里需要注意的是，因为每次分配span时都会重新映射，所以不需要担心映射到旧的span
		if (ret == nullptr)
		{
			break;
		}
		//前面的span在使用，
		if (ret->_isUsed == true)
		{
			break;
		}
		//如果合并之后大于128页，就不合并
		if (ret->_n + span->_n > NPAGES - 1)
		{
			break;
		}
		span->_pageId = ret->_pageId;
		span->_n += ret->_n;
		_spanList[ret->_n].Erease(ret);
		_spanPool.Delete(ret);
	}
	while (true)
	{
		PageID nextId = span->_pageId + span->_n;
		//auto ret = _idSpanMap.find(nextId);
		auto ret = (Span*)_idSpanMap.get(nextId);
		if (ret == nullptr)
		{
			break;
		}
		//后面的span在使用，
		Span* span1 = ret;
		if (ret->_isUsed == true)
		{
			break;
		}
		//如果合并之后大于128页，就不合并
		if (ret->_n + span->_n > NPAGES - 1)
		{
			break;
		}
		span->_n += ret->_n;
		_spanList[ret->_n].Erease(ret);
		_spanPool.Delete(ret);
	}

	_spanList[span->_n].PushFront(span);
	span->_isUsed = false;
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.Set(span->_pageId, span);
	_idSpanMap.Set(span->_pageId + span->_n - 1, span);
}