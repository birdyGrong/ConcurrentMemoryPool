#pragma once
#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"

//每个线程无锁地创建thread_cache

static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)
	{
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kPage = alignSize >> PAGE_SHIFT;

		PageCache::GetInstance()->_mtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kPage);
		PageCache::GetInstance()->_mtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		if (tls_threadcache == nullptr)
		{
			static ObjectPool<ThreadCache> tcpool;
			tls_threadcache = tcpool.New();
			//tls_threadcache = new ThreadCache;
		}
		return tls_threadcache->Allocate(size);
	}

}

static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;
	if (size > MAX_BYTES)
	{
		PageCache::GetInstance()->_mtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_mtx.unlock();
	}
	else
	{
		assert(tls_threadcache);
		tls_threadcache->Deallocate(ptr, size);
	}
}