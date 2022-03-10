#pragma once
#include<iostream>
#include<vector>
#include<ctime>
#include<cassert>
#include<mutex>
#include<thread>
#include<algorithm>
#include<unordered_map>
#include <atomic>
using std::cout;
using std::endl;

//定义一个MAX_BYTES,如果申请的内存块小于MAX_BYTES,就从thread_cache申请
//大于MAX_BYTES, 就直接从page_cache中申请
static const size_t MAX_BYTES = 256 * 1024;

//thread_cache和central cache中自由链表哈希桶的表大小
static const size_t NFREELISTS = 208;

//页大小转换偏移量，一页为2^13K，也就是8KB
static const size_t PAGE_SHIFT = 13;

//PageCache 哈希表的大小
static const size_t NPAGES = 129;

//页编号类型，32位是4Byte，64位是8byte
#ifdef _WIN64
	typedef unsigned long long PageID;
#elif _WIN32
	typedef size_t PageID;
#else
	//
#endif

#ifdef _WIN32
	#include<Windows.h>
	#undef min
#else
//在linux下申请空间的头文件
#endif

//直接去堆上按页申请空间
inline static void* SystemAlloc(size_t kpage)
{
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage * (1 << PAGE_SHIFT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	//在linux下申请
#endif
	if (ptr == nullptr)
		throw std::bad_alloc();
	return ptr;
}

inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	//linux下的逻辑
#endif
}

//获取内存对象中存储的头4bit或者8bit值，即链接下一个对象的地址
static inline void*& Nextobj(void* obj)
{
	return *((void**)obj);
}

class FreeList
{
public:
	void Push(void* obj)
	{
		Nextobj(obj) = _head;
		_head = obj;
		++_size;
	}

	void* Pop()
	{
		void* obj = _head;
		_head = Nextobj(_head);
		--_size;
		return obj;
	}

	bool IsEmpty()
	{
		if (_head != nullptr)
		{
			int i = 0;
		}
		return _head == nullptr;
	}

	size_t& MaxSize()
	{
		return _max_size;
	}

	void PushRange(void* start, void* end, size_t rangeNum)
	{
		Nextobj(end) = _head;
		_head = start;
		_size += rangeNum;
	}

	void PopRange(void*& start, void*& end, size_t rangNum)
	{
		assert(rangNum <= _size);
		start = _head;
		end = start;
		for (size_t i = 0; i < rangNum - 1; i++)
		{
			end = Nextobj(end);
		}
		_head = Nextobj(end);
		Nextobj(end) = nullptr;
		_size -= rangNum;
	}

	size_t Size()
	{
		return _size;
	}

private:
	void* _head = nullptr;
	//一次能批量申请的内存块的最大数量
	size_t _max_size = 1;
	//自由链表中的内存块的数量
	size_t _size = 0;
};


/********************************************************
 * 自由链表的哈希桶位置和申请内存块大小的映射关系       *
 * ThreadCache是哈希桶结构，每个桶对应了一个自由链表。  *
 * 每个自由链表中，分配的块的大小是固定的。             *
 * 每个桶是按照自由链表中内存块的大小来对应映射位置的。 *
 * 如果从1B到256KB每个字节都分配一个哈希桶，这并不现实。*
 * 因此，可以采用对齐数的方式。                         *
 ********************************************************/

class SizeClass
{
public:
	// 整体控制在最多10%左右的内碎片浪费
	// [1,128]                  8byte对齐        [0,16)号哈希桶
	// [128+1,1024]             16byte对齐       [16,72)号哈希桶
	// [1024+1,8*1024]          128byte对齐      [72,128)号哈希桶
	// [8*1024+1,64*1024]       1024byte对齐     [128,184)号哈希桶
	// [64*1024+1,256*1024]     8*1024byte对齐   [184,208)号哈希桶

	//给定需要的字节数和对齐数，返回实际申请的内存块大小
	static inline size_t _RoundUp(size_t bytes, size_t align)
	{
		return (bytes + align - 1)&~(align - 1);
	}

	static inline size_t RoundUp(size_t bytes)
	{
		if (bytes <= 128)
		{
			return _RoundUp(bytes, 8);
		}
		else if (bytes <= 1024)
		{
			return _RoundUp(bytes, 16);
		}
		else if (bytes <= 8 * 1024)
		{
			return _RoundUp(bytes, 128);
		}
		else if (bytes <= 64 * 1024)
		{
			return _RoundUp(bytes, 1024);
		}
		else
		{
			return _RoundUp(bytes, 1 << PAGE_SHIFT);
		}
		return -1;
	}

	//align_shift是对齐数的偏移量，如对齐数是8KB，偏移量就是13
	static inline size_t _Index(size_t bytes, size_t align_shift)
	{
		// 最后 -1 的意思是向下取整, 因为哈希桶编号是从0开始的
		return ((bytes + (1 << align_shift) - 1) >> align_shift) - 1;
	}

	//给定所申请的内存块大小，寻找在哪个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);

		//用一个数组存储，每个区间有多少个桶
		static int group[4] = { 16, 56, 56, 56 };
		if (bytes <= 128)
		{
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024)
		{
			return _Index(bytes - 128, 4) + group[0];
		}
		else if (bytes <= 8 * 1024)
		{
			return _Index(bytes - 1024, 7) + group[0] + group[1];
		}
		else if (bytes <= 64 * 1024)
		{
			return _Index(bytes - 8 * 1024, 10) + group[0] + group[1] + group[2];
		}
		else if(bytes <= 256 * 1024)
		{
			return _Index(bytes - 64 * 1024, 13) + group[0] + group[1] + group[2] + group[3];
		}
		else
		{
			assert(false);
		}

		return 0;
	}

	//一次从中心缓存获取多少个
	static size_t NumMovSize(size_t size)
	{
		if (size == 0)
			return 0;
		//大对象一次获得的少，小对象一次获得的多
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;

		return num;
	}

	static size_t NumMovePage(size_t size)
	{
		int num = NumMovSize(size);
		int npage = num * size;
		npage >>= PAGE_SHIFT;
		if (npage == 0)
			return 1;
		return npage;
	}
};

// PageCache和CentralCache的桶挂的都是以页为单位Span链表,Span链表是以带头双向循环链表的形式存在的
class Span
{
public:
	//起始页号
	PageID _pageId = 0;
	//Span对象中的页数
	size_t _n = 0;
	
	Span* _next = nullptr;
	Span* _prev = nullptr;

	//Span会被切分成自由链表，方便ThreadCache直接在相应的桶中取出内存块
	void* _freeList = nullptr;

	//目前该Span对象已经分配了多少内存块给ThreadCache，当_useCount == 0,说明ThreadCache已经把所有的内存块还回来了
	size_t _useCount = 0;

	//切出来的单个对象的大小。
	size_t _objSize = 0;

	//是否被使用
	bool _isUsed = false;

};

class SpanList
{
public:
	SpanList() :_head(new Span)
	{
		_head->_next = _head;
		_head->_prev = _head;
	}

	void Insert(Span* cur, Span* newSpan)
	{
		assert(cur);
		Span* prev = cur->_prev;
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = cur;
		cur->_prev = newSpan;
	}

	void Erease(Span* cur)
	{
		assert(cur);
		assert(cur != _head);

		Span* prev = cur->_prev;
		Span* next = cur->_next;

		prev->_next = next;
		next->_prev = prev;
	}

	bool IsEmpty()
	{
		return _head->_next == _head;
	}

	Span* Begin()
	{
		return _head->_next;
	}

	Span* End()
	{
		return _head;
	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	Span* PopFront()
	{
		Span* span = _head->_next;
		Erease(span);
		return span;
	}

public:
	//CentralCache在访问SpanList时，要上锁
	std::mutex _mtx;
private:
	Span* _head;
};

