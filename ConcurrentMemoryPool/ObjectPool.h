#pragma once
#include"Common.h"


template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;
		if (_freeList)
		{
			obj = (T*)_freeList;
			_freeList = *((void**)_freeList);
		}
		else
		{
			if (_leftBytes < sizeof(T))
			{
				_leftBytes = 128 * 1024;
				_memory = (char*)SystemAlloc (_leftBytes >> PAGE_SHIFT);
				//如果申请失败，抛异常
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}
			obj = (T*)_memory;
			//如果按照 sizeof(T)来对块大小进行分配
			//那么32bit下，小于4字节的无法再自由链表中串联
			//64bit下，小于4字节的无法再自由链表中串联
			//因此至少要分配指针大小
			/*_memory += sizeof(T);
			_leftBytes -= sizeof(T);*/
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_leftBytes -= objSize;
		}
		new(obj) T;
		return obj;
	}

	void Delete(T* obj)
	{
		obj -> ~T();
		*((void**)obj) = _freeList;
		_freeList = obj;
	}
private:
	char* _memory = nullptr;
	//freeList 的前指针个大小存放的下一个块的地址
	void* _freeList = nullptr;
	size_t _leftBytes = 0;
};
