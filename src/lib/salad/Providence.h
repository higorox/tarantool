#pragma once
#include <stddef.h>
#include <stdlib.h>

#ifdef __OPTIMIZE__
#define prov_assert(e)
#else
#define prov_assert(e) assert(e)
#include <assert.h>
#endif

// Compile time utils
#ifndef CT_ASSERT
#define CT_ASSERT(e) do { typedef char __ct_assert[(e) ? 1 : -1]; } while(0)
#endif

namespace {
template<size_t t>
struct CLog2 {
	enum { res = CLog2<t / 2>::res + 1 };
};

template<>
struct CLog2<1> {
	enum { res = 0 };
};
} // namespace {
// Compile time utils END

template<size_t AllocSize>
struct CStupidAllocator {
	static void *Alloc()
	{
		return malloc(AllocSize);
	}
	static void Free(void *data)
	{
		free(data);
	}
};

template<size_t AllocSize, size_t AllignedSize>
struct CAllignedAllocator {
	static void *Alloc()
	{
		CT_ASSERT((AllignedSize & (AllignedSize - 1)) == 0);
		typedef unsigned long ulongptr_t;
		CT_ASSERT(sizeof(void *) == sizeof(ulongptr_t));

		void *data = malloc(AllocSize + AllignedSize + sizeof(int));
		ulongptr_t ulptr = reinterpret_cast<ulongptr_t>(data);
		int paddingSize = AllignedSize - ((ulptr + sizeof(int)) & (AllignedSize - 1));
		prov_assert(((ulptr + paddingSize + sizeof(int)) & (AllignedSize - 1)) == 0);

		char *cdata = static_cast<char *>(data);
		*reinterpret_cast<int *>(cdata + paddingSize) = paddingSize;
		return cdata + paddingSize + sizeof(int);
	}
	static void Free(void *data)
	{
		char *cdata = static_cast<char *>(data);
		int paddingSize = *reinterpret_cast<int *>(cdata - sizeof(int));
		free(cdata - sizeof(paddingSize) - sizeof(int));
	}
};

template<class CAllocator_t, class ID_t, int Level, size_t ItemSize, size_t AllocSize>
struct CProvidenceHelper {
	typedef CProvidenceHelper<CAllocator_t, ID_t, Level - 1, ItemSize, AllocSize> CChild_t;
	enum {
		ChildrenCount = AllocSize / sizeof(void *),
		ChildCapacity = CChild_t::MaxCapacity,
		MaxCapacity = ChildrenCount * ChildCapacity,
		CurrentIndexShift = CLog2<ChildCapacity>::res,
		NextIndexMask = ChildCapacity - 1
	};
	static void *Alloc(void *& data, ID_t createdCount) // you should increment createdCount immidiately
	{
		prov_assert(createdCount < MaxCapacity);
		if (createdCount == 0)
			data = CAllocator_t::Alloc();
		void **childrenData = static_cast<void **>(data);
		ID_t childIndex = createdCount >> CurrentIndexShift;
		ID_t childCreated = createdCount & NextIndexMask;
		return CChild_t::Alloc(childrenData[childIndex], childCreated);
	}
	static void *Get(void * data, ID_t index)
	{
		prov_assert(index < MaxCapacity);
		void **childrenData = static_cast<void **>(data);
		ID_t childIndex = index >> CurrentIndexShift;
		ID_t indexInChild = index & NextIndexMask;
		return CChild_t::Get(childrenData[childIndex], indexInChild);
	}
	static void FreeAll(void *data, ID_t createdCount) // you should set createdCount to 0 immidiately
	{
		prov_assert(createdCount <= MaxCapacity);
		if (createdCount) {
			void **childrenData = static_cast<void **>(data);
			ID_t childIndex = createdCount >> CurrentIndexShift;
			ID_t childCreated = createdCount & NextIndexMask;
			for (ID_t i = 0; i < childIndex; i++)
				CChild_t::FreeAll(childrenData[i], ChildCapacity);
			CChild_t::FreeAll(childrenData[childIndex], childCreated);
			CAllocator_t::Free(data);
		}
	}
};

template<class CAllocator_t, class ID_t, size_t ItemSize, size_t AllocSize>
struct CProvidenceHelper<CAllocator_t, ID_t, 1, ItemSize, AllocSize> {
	typedef char ct_assert_typedef[AllocSize % ItemSize == 0 ? 1 : -1];
	enum {
		MaxCapacity = AllocSize / ItemSize
	};
	static void *Alloc(void *& data, ID_t createdCount) // you should increment createdCount immidiately
	{
		prov_assert(createdCount < MaxCapacity);
		if (createdCount == 0)
			data = CAllocator_t::Alloc();
		char *itemsBuf = static_cast<char *>(data);
		size_t offset = createdCount * ItemSize;
		return static_cast<void *>(itemsBuf + offset);
	}
	static void *Get(void * data, ID_t index)
	{
		prov_assert(index < MaxCapacity);
		char *itemsBuf = static_cast<char *>(data);
		size_t offset = index * ItemSize;
		return static_cast<void *>(itemsBuf + offset);
	}
	static void FreeAll(void *data, ID_t createdCount) // you should set createdCount to 0 immidiately
	{
		prov_assert(createdCount <= MaxCapacity);
		if (createdCount) {
			CAllocator_t::Free(data);
		}
	}
};

template<class CAllocator_t, class ID_t, int RecurseCount, size_t ItemSize, size_t AllocSize>
class CProvidence {
public:
	enum {
		MaxCapacity = CProvidenceHelper<CAllocator_t, ID_t, RecurseCount, ItemSize, AllocSize>::MaxCapacity
	};

	CProvidence() : createdCount(0) {}
	~CProvidence() { FreeAll(); }

	void *Alloc(ID_t &index)
	{
		index = createdCount;
		return CProvidenceHelper<CAllocator_t, ID_t, RecurseCount, ItemSize, AllocSize>::Alloc(data, createdCount++);
	}

	void *Get(ID_t index) const
	{
		prov_assert(index > 0 || index == 0);
		prov_assert(index < createdCount);
		return CProvidenceHelper<CAllocator_t, ID_t, RecurseCount, ItemSize, AllocSize>::Get(data, index);
	}

	ID_t Count() const
	{
		return createdCount;
	}

	void FreeAll()
	{
		CProvidenceHelper<CAllocator_t, ID_t, RecurseCount, ItemSize, AllocSize>::FreeAll(data, createdCount);
		createdCount = 0;
	}
private:
	void *data;
	ID_t createdCount;
};
