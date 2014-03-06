#pragma once
#include "providence.h"
#include <string.h>

// Tree debug macro (may be predefined manually)
#if defined(_DEBUG)
#define BTREE_DEBUG
#elif !defined(__OPTIMIZE__)
#define BTREE_DEBUG
#endif

// Assert definition
#ifdef BTREE_DEBUG
// Debug version calls assert(..)
#include <assert.h>
#define btree_assert(e) assert(e)
#else
// Release verions does nothing
#define btree_assert(e) do {} while(0)
#endif

// Data moving
#ifdef BTREE_DEBUG
// Debug version checks (types at compile time) and (buffer overflow an runtime) additionally
#define DATAMOVE(dst, src, num, dstNode, srcNode) debugDataMove(dst, src, num, dstNode, srcNode)
#else
// Release version just moves memory
#define DATAMOVE(dst, src, num, dstNode, srcNode) memmove(dst, src, (num) * sizeof((dst)[0]))
#endif

// Compile time utils
#ifndef CT_ASSERT
#define CT_ASSERT(e) do { typedef char __ct_assert[(e) ? 1 : -1]; } while(0)
#endif

template<class T, class U>
struct CTTypeIs { enum { res = 0 }; };
template<class T>
struct CTTypeIs<T, T> { enum { res = 1 }; };

#define CA_TYPEIS(T, U) (CTTypeIs<T, U>::res != 0)
#define CA_TYPEISOR(T, U, V) ((CTTypeIs<T, U>::res || CTTypeIs<T, V>::res) != 0)

#define CT_ASSERT_TYPEIS(T, U) do { typedef char __ct_assert[(CTTypeIs<T, U>::res) ? 1 : -1]; } while(0)
#define CT_ASSERT_TYPEISOR(T, U, V) do { typedef char __ct_assert[(CTTypeIs<T, U>::res || CTTypeIs<T, V>::res) ? 1 : -1]; } while(0)
// Compile time utils END

// The Tree
template<class Elem_t, size_t NodeSize, class CAllocator, size_t AllocSize, int AllocLevel, class CComp, class CompPar_t>
class CCensoredTree {
public:
// Basic types
	typedef unsigned int NodeId_t;
	typedef short int Pos_t;
	typedef CCensoredTree<Elem_t, NodeSize, CAllocator, AllocSize, AllocLevel, CComp, CompPar_t> This_t;

	enum ChunkType {
		CT_Garbage,
		CT_Inner,
		CT_Leaf
	};

	struct CChunk {
		CChunk(Pos_t _Type) : Type(_Type) {}
		Pos_t Type;
	};

	struct CNode : CChunk {
		CNode(Pos_t _Type) : CChunk(_Type) {}
		Pos_t ElemCount;
	};

	struct CGarbage : CNode {
		CGarbage() : CNode(CT_Garbage) {}
		NodeId_t Id;
		CGarbage *Next;
	};

	struct CLeaf : CNode {
		enum { ElemMaxCount = (NodeSize - sizeof(CNode)) / sizeof(Elem_t) };
		Elem_t Elems[ElemMaxCount];

		CLeaf() : CNode(CT_Leaf) {}

		Elem_t &LastElem() { btree_assert(CNode::ElemCount > 0 && CNode::ElemCount <= ElemMaxCount); return Elems[CNode::ElemCount - 1]; }
		const Elem_t &LastElem() const { btree_assert(CNode::ElemCount > 0 && CNode::ElemCount <= ElemMaxCount); return Elems[CNode::ElemCount - 1]; }
		Pos_t SpareCount() { return ElemMaxCount - CNode::ElemCount; }
		Pos_t OverCount() { return CNode::ElemCount - ElemMaxCount * 2 / 3; }
	};
	struct CInner : CNode {
		enum { ElemMaxCount = (NodeSize - sizeof(CNode) + sizeof(Elem_t)) / (sizeof(Elem_t) + sizeof(NodeId_t)) };
		NodeId_t ChildIds[ElemMaxCount];
		Elem_t Elems[ElemMaxCount - 1];

		CInner() : CNode(CT_Inner) {}

		Pos_t SpareCount() { return ElemMaxCount - CNode::ElemCount; }
		Pos_t OverCount() { return CNode::ElemCount - ElemMaxCount * 2 / 3; }
	};

private:
// Data
	CNode *root;
	NodeId_t rootId;
	NodeId_t leafCount, internalCount, garbageCount;
	NodeId_t depth;
	size_t count;
	CGarbage *garbage;
	CompPar_t param;
	Elem_t maxElem;
	CProvidence<CAllocator, NodeId_t, AllocLevel, NodeSize, AllocSize> providence;

public:
// Construction
	CCensoredTree() :
		root(0),
		leafCount(0),
		internalCount(0),
		garbageCount(0),
		depth(0),
		count(0),
		garbage(0)
	{
		CT_ASSERT(sizeof(CGarbage) <= NodeSize);
		CT_ASSERT(sizeof(CLeaf) <= NodeSize);
		CT_ASSERT(sizeof(CInner) <= NodeSize);
		CT_ASSERT(CInner::ElemMaxCount >= 4); // for 1/4 of node count not to be zero
	}
	CCensoredTree(const CompPar_t &_param) :
		root(0),
		leafCount(0),
		internalCount(0),
		garbageCount(0),
		depth(0),
		count(0),
		garbage(0),
		param(_param)
	{
	}

// Main interface
public:
	template<class Key_t>
	Elem_t *Find(const Key_t &key)
	{
		if (!root)
			return 0;
		CNode *node = root;
		bool exact = false;
		for (NodeId_t i = 0; i < depth - 1; i++) {
			CInner *inner = static_cast<CInner *>(node);
			Pos_t pos;
			if (exact)
				pos = inner->ElemCount - 1;
			else
				pos = findInsPoint(inner, key, exact);
			node = getNode(inner->ChildIds[pos]);
		}

		CLeaf *leaf = static_cast<CLeaf *>(node);
		Pos_t pos;
		if (exact)
			pos = leaf->ElemCount - 1;
		else
			pos = findInsPoint(leaf, key, exact);
		if (exact)
			return leaf->Elems + pos;
		else
			return 0;
	}

	// *replaced remains untouched during insert (not replace)
	bool Insert(const Elem_t &newElem, bool allowInsert = true, bool allowReplace = true, Elem_t *replaced = 0)
	{
		btree_assert(allowInsert || allowReplace);
		if (!root)
			return insertFirstElem(newElem, allowInsert);
#ifdef WIN32
		CNodeExtended<CInner> pathExts[20];
		btree_assert(depth < sizeof(pathExts) / sizeof(pathExts[0]));
#else
		CNodeExtended<CInner> pathExts[depth - 1];
#endif
		CNodeExtended<CLeaf> ext;
		bool exact;
		collectPath(newElem, pathExts, ext, exact);

		if ((exact && !allowReplace) || (!exact && !allowInsert))
			return false;

		if (exact)
			return processReplace(ext, newElem, replaced);
		else
			return processInsert(ext, newElem);
	}

	bool Delete(const Elem_t &elem)
	{
		if (!root)
			return false;
#ifdef WIN32
		CNodeExtended<CInner> pathExts[20];
		btree_assert(depth < sizeof(pathExts) / sizeof(pathExts[0]));
#else
		CNodeExtended<CInner> pathExts[depth - 1];
#endif
		CNodeExtended<CLeaf> ext;
		bool exact;
		collectPath(elem, pathExts, ext, exact);

		if (!exact)
			return false;

		return processDelete(ext);
	}

	size_t Count() const
	{
		return count;
	}

private:
	template<class Node_t>
	struct CNodeExtended {
		CNodeExtended() : node(0), nodeId(0), insertionPoint(0), posInParent(0), parentInfo(0), maxElemCopy(0) {}
		Node_t *node;
		NodeId_t nodeId;
		Pos_t insertionPoint; // for inner node use only by deletion
		Pos_t posInParent;
		CNodeExtended<CInner> *parentInfo;
		Elem_t *maxElemCopy;
	};

	// Basic operation on nodes: inserting, deleting, moving to left and right nodes, and (inserting and moving)
	bool simpleInsert(CNodeExtended<CLeaf> &ext, const Elem_t &newElem)
	{
		CLeaf *a = ext.node;
		Pos_t pos = ext.insertionPoint;

		btree_assert(pos >= 0);
		btree_assert(pos <= a->ElemCount);
		btree_assert(a->ElemCount < a->ElemMaxCount);

		DATAMOVE(a->Elems + pos + 1, a->Elems + pos, a->ElemCount - pos, a, a);
		a->Elems[pos] = newElem;
		*ext.maxElemCopy = a->Elems[a->ElemCount];

		a->ElemCount++;
		count++;
		return true;
	}

	template<class Node_t>
	bool simpleInsert(CNodeExtended<CInner> &ext, const CNodeExtended<Node_t> &newExt)
	{
		CInner *a = ext.node;
		Pos_t pos = newExt.posInParent;

		btree_assert(pos >= 0);
		btree_assert(pos <= a->ElemCount);
		btree_assert(a->ElemCount < a->ElemMaxCount);

		if (pos < a->ElemCount) {
			DATAMOVE(a->Elems + pos + 1, a->Elems + pos, a->ElemCount - pos - 1, a, a);
			a->Elems[pos] = *newExt.maxElemCopy;
			DATAMOVE(a->ChildIds + pos + 1, a->ChildIds + pos, a->ElemCount - pos, a, a);
		} else {
			a->Elems[pos - 1] = *ext.maxElemCopy;
			*ext.maxElemCopy = *newExt.maxElemCopy;
		}
		a->ChildIds[pos] = newExt.nodeId;

		a->ElemCount++;
		return true;
	}

	bool simpleDelete(CNodeExtended<CLeaf> &ext)
	{
		CLeaf *a = ext.node;
		Pos_t pos = ext.insertionPoint;

		btree_assert(pos >= 0);
		btree_assert(pos < a->ElemCount);

		DATAMOVE(a->Elems + pos, a->Elems + pos + 1, a->ElemCount - 1 - pos, a, a);

		a->ElemCount--;

		if (a->ElemCount > 0)
			*ext.maxElemCopy = a->Elems[a->ElemCount - 1];

		count--;
		return true;
	}

	bool simpleDelete(CNodeExtended<CInner> &ext)
	{
		CInner *a = ext.node;
		Pos_t pos = ext.insertionPoint;

		btree_assert(pos >= 0);
		btree_assert(pos < a->ElemCount);

		if (pos < a->ElemCount - 1) {
			DATAMOVE(a->Elems + pos, a->Elems + pos + 1,   a->ElemCount - 2 - pos, a, a);
			DATAMOVE(a->ChildIds + pos, a->ChildIds + pos + 1,   a->ElemCount - 1 - pos, a, a);
		} else if (pos > 0) {
			*ext.maxElemCopy = a->Elems[pos - 1];
		}

		a->ElemCount--;
		return true;
	}

	template<bool MoveToEmpty, bool MoveAll>
	bool moveRight(CNodeExtended<CLeaf> &aExt, CNodeExtended<CLeaf> &bExt, Pos_t num)
	{
		CLeaf *a = aExt.node;
		CLeaf *b = bExt.node;

		btree_assert(num > 0);
		btree_assert(a->ElemCount >= num);
		btree_assert(b->ElemCount + num <= b->ElemMaxCount);
		btree_assert(MoveToEmpty == (b->ElemCount == 0));
		btree_assert(MoveAll == (a->ElemCount == num));

		if (!MoveToEmpty)
			DATAMOVE(b->Elems + num, b->Elems, b->ElemCount, b, b);
		DATAMOVE(b->Elems, a->Elems + a->ElemCount - num, num, b, a);

		a->ElemCount -= num;
		b->ElemCount += num;

		if (!MoveAll)
			*aExt.maxElemCopy = a->LastElem();
		if (MoveToEmpty)
			*bExt.maxElemCopy = b->LastElem();

		return true;
	}

	template<bool MoveToEmpty, bool MoveAll>
	bool moveRight(CNodeExtended<CInner> &aExt, CNodeExtended<CInner> &bExt, Pos_t num)
	{
		CInner *a = aExt.node;
		CInner *b = bExt.node;

		btree_assert(num > 0);
		btree_assert(a->ElemCount >= num);
		btree_assert(b->ElemCount + num <= b->ElemMaxCount);
		btree_assert(MoveToEmpty == (b->ElemCount == 0));
		btree_assert(MoveAll == (a->ElemCount == num));

		if (!MoveToEmpty)
			DATAMOVE(b->ChildIds + num, b->ChildIds, b->ElemCount, b, b);
		DATAMOVE(b->ChildIds, a->ChildIds + a->ElemCount - num, num, b, a);

		if (!MoveToEmpty)
			DATAMOVE(b->Elems + num, b->Elems, b->ElemCount - 1, b, b);
		DATAMOVE(b->Elems, a->Elems + a->ElemCount - num, num - 1, b, a);
		if (MoveToEmpty)
			*bExt.maxElemCopy = *aExt.maxElemCopy;
		else
			b->Elems[num - 1] = *aExt.maxElemCopy;
		if (!MoveAll)
			*aExt.maxElemCopy = a->Elems[a->ElemCount - num - 1];

		a->ElemCount -= num;
		b->ElemCount += num;
		return true;
	}

	template<bool MoveToEmpty, bool MoveAll>
	bool moveLeft(CNodeExtended<CLeaf> &aExt, CNodeExtended<CLeaf> &bExt, Pos_t num)
	{
		CLeaf *a = aExt.node;
		CLeaf *b = bExt.node;

		btree_assert(num > 0);
		btree_assert(b->ElemCount >= num);
		btree_assert(a->ElemCount + num <= a->ElemMaxCount);
		btree_assert(MoveToEmpty == (a->ElemCount == 0));
		btree_assert(MoveAll == (b->ElemCount == num));

		DATAMOVE(a->Elems + a->ElemCount, b->Elems, num, a, b);
		if (!MoveAll)
			DATAMOVE(b->Elems, b->Elems + num, b->ElemCount - num, b, b);

		a->ElemCount += num;
		b->ElemCount -= num;
		*aExt.maxElemCopy = a->LastElem();

		return true;
	}

	template<bool MoveToEmpty, bool MoveAll>
	bool moveLeft(CNodeExtended<CInner> &aExt, CNodeExtended<CInner> &bExt, Pos_t num)
	{
		CInner *a = aExt.node;
		CInner *b = bExt.node;

		btree_assert(num > 0);
		btree_assert(b->ElemCount >= num);
		btree_assert(a->ElemCount + num <= a->ElemMaxCount);
		btree_assert(MoveToEmpty == (a->ElemCount == 0));
		btree_assert(MoveAll == (b->ElemCount == num));

		DATAMOVE(a->ChildIds + a->ElemCount, b->ChildIds, num, a, b);
		if (!MoveAll)
			DATAMOVE(b->ChildIds, b->ChildIds + num, b->ElemCount - num, b, b);

		if (!MoveToEmpty)
			a->Elems[a->ElemCount - 1] = *aExt.maxElemCopy;
		DATAMOVE(a->Elems + a->ElemCount, b->Elems, num - 1, a, b);
		if (MoveAll) {
			*aExt.maxElemCopy = *bExt.maxElemCopy;
		} else {
			*aExt.maxElemCopy = b->Elems[num - 1];
			DATAMOVE(b->Elems, b->Elems + num, b->ElemCount - num - 1, b, b);
		}

		a->ElemCount += num;
		b->ElemCount -= num;
		return true;
	}

	template<bool MoveToEmpty, bool MoveAll>
	bool moveRightInsert(CNodeExtended<CLeaf> &aExt, CNodeExtended<CLeaf> &bExt, Pos_t num, const Elem_t &newElem)
	{
		CLeaf *a = aExt.node;
		CLeaf *b = bExt.node;
		Pos_t pos = aExt.insertionPoint;

		btree_assert(num > 0);
		btree_assert(a->ElemCount >= num - 1);
		btree_assert(b->ElemCount + num <= b->ElemMaxCount);
		btree_assert(pos <= a->ElemCount);
		btree_assert(pos >= 0);
		btree_assert(MoveToEmpty == (b->ElemCount == 0));
		btree_assert(MoveAll == (a->ElemCount == num - 1));

		if (!MoveToEmpty)
			DATAMOVE(b->Elems + num, b->Elems, b->ElemCount, b, b);

		Pos_t midPartSize = a->ElemCount - pos;
		if (midPartSize >= num) {
			// In fact insert to 'a' node
			DATAMOVE(b->Elems, a->Elems + a->ElemCount - num, num, b, a);
			DATAMOVE(a->Elems + pos + 1, a->Elems + pos, midPartSize - num, a, a);
			a->Elems[pos] = newElem;
		} else {
			// In fact insert to 'b' node
			Pos_t newPos = num - midPartSize - 1; // Can be 0
			DATAMOVE(b->Elems, a->Elems + a->ElemCount - num + 1, newPos, b, a);
			b->Elems[newPos] = newElem;
			DATAMOVE(b->Elems + newPos + 1, a->Elems + pos, midPartSize, b, a);
		}

		a->ElemCount -= (num - 1);
		b->ElemCount += num;
		if (!MoveAll)
			*aExt.maxElemCopy = a->LastElem();
		if (MoveToEmpty)
			*bExt.maxElemCopy = b->LastElem();
		count++;
		return true;
	}

	template<bool MoveToEmpty, bool MoveAll, class Node_t>
	bool moveRightInsert(CNodeExtended<CInner> &aExt, CNodeExtended<CInner> &bExt, Pos_t num, const CNodeExtended<Node_t> &newExt)
	{
		CInner *a = aExt.node;
		CInner *b = bExt.node;
		Pos_t pos = newExt.posInParent;

		btree_assert(num > 0);
		btree_assert(a->ElemCount >= num - 1);
		btree_assert(b->ElemCount + num <= b->ElemMaxCount);
		btree_assert(pos <= a->ElemCount);
		btree_assert(pos >= 0);
		btree_assert(MoveToEmpty == (b->ElemCount == 0));
		btree_assert(MoveAll == (a->ElemCount == num - 1));

		if (!MoveToEmpty) {
			DATAMOVE(b->ChildIds + num, b->ChildIds, b->ElemCount, b, b);
			DATAMOVE(b->Elems + num, b->Elems, b->ElemCount - 1, b, b);
		}

		Pos_t midPartSize = a->ElemCount - pos;
		if (midPartSize > num) {
			// In fact insert to 'a' node, to the internal position
			DATAMOVE(b->ChildIds, a->ChildIds + a->ElemCount - num, num, b, a);
			DATAMOVE(a->ChildIds + pos + 1, a->ChildIds + pos, midPartSize - num, a, a);
			a->ChildIds[pos] = newExt.nodeId;

			DATAMOVE(b->Elems, a->Elems + a->ElemCount - num, num - 1, b, a);
			if (MoveToEmpty)
				*bExt.maxElemCopy = *aExt.maxElemCopy;
			else
				b->Elems[num - 1] = *aExt.maxElemCopy;

			*aExt.maxElemCopy = a->Elems[a->ElemCount - num - 1];
			DATAMOVE(a->Elems + pos + 1, a->Elems + pos, midPartSize - num - 1, a, a);
			a->Elems[pos] = *newExt.maxElemCopy;
		} else if (midPartSize == num) {
			// In fact insert to 'a' node, to the last position
			DATAMOVE(b->ChildIds, a->ChildIds + a->ElemCount - num, num, b, a);
			DATAMOVE(a->ChildIds + pos + 1, a->ChildIds + pos, midPartSize - num, a, a);
			a->ChildIds[pos] = newExt.nodeId;

			DATAMOVE(b->Elems, a->Elems + a->ElemCount - num, num - 1, b, a);
			if (MoveToEmpty)
				*bExt.maxElemCopy = *aExt.maxElemCopy;
			else
				b->Elems[num - 1] = *aExt.maxElemCopy;

			*aExt.maxElemCopy = *newExt.maxElemCopy;
		} else {
			// In fact insert to 'b' node
			Pos_t newPos = num - midPartSize - 1; // Can be 0
			DATAMOVE(b->ChildIds, a->ChildIds + a->ElemCount - num + 1, newPos, b, a);
			b->ChildIds[newPos] = newExt.nodeId;
			DATAMOVE(b->ChildIds + newPos + 1, a->ChildIds + pos, midPartSize, b, a);

			if (pos == a->ElemCount) {
				// +1
				if (MoveToEmpty)
					*bExt.maxElemCopy = *newExt.maxElemCopy;
				else
					b->Elems[num - 1] = *newExt.maxElemCopy;
				if (num > 1) {
					// +(num - 2)
					DATAMOVE(b->Elems, a->Elems + a->ElemCount - num + 1, num - 2, b, a);
					// +1
					b->Elems[num - 2] = *aExt.maxElemCopy;

					if (!MoveAll)
						*aExt.maxElemCopy = a->Elems[a->ElemCount - num];
				}
			} else {
				btree_assert(num > 1);

				DATAMOVE(b->Elems, a->Elems + a->ElemCount - num + 1, num - midPartSize - 1, b, a);
				b->Elems[newPos] = *newExt.maxElemCopy;
				DATAMOVE(b->Elems + newPos + 1, a->Elems + pos, midPartSize - 1, b, a);
				if (MoveToEmpty)
					*bExt.maxElemCopy = *aExt.maxElemCopy;
				else
					b->Elems[num - 1] = *aExt.maxElemCopy;

				if (!MoveAll)
					*aExt.maxElemCopy = a->Elems[a->ElemCount - num];
			}
		}

		a->ElemCount -= (num - 1);
		b->ElemCount += num;
		return true;
	}

	template<bool MoveToEmpty, bool MoveAll>
	bool moveLeftInsert(CNodeExtended<CLeaf> &aExt, CNodeExtended<CLeaf> &bExt, Pos_t num, const Elem_t &newElem)
	{
		CLeaf *a = aExt.node;
		CLeaf *b = bExt.node;
		Pos_t pos = bExt.insertionPoint;

		btree_assert(num > 0);
		btree_assert(b->ElemCount >= num - 1);
		btree_assert(a->ElemCount + num <= a->ElemMaxCount);
		btree_assert(pos >= 0);
		btree_assert(pos <= b->ElemCount);
		btree_assert(MoveToEmpty == (a->ElemCount == 0));
		btree_assert(MoveAll == (b->ElemCount == num - 1));

		if (pos >= num) {
			// In fact insert to 'b' node
			Pos_t newPos = pos - num; // Can be 0
			DATAMOVE(a->Elems + a->ElemCount, b->Elems, num, a, b);
			DATAMOVE(b->Elems, b->Elems + num, newPos, b, b);
			b->Elems[newPos] = newElem;
			DATAMOVE(b->Elems + newPos + 1, b->Elems + pos, b->ElemCount - pos, b, b);

		} else {
			// In fact insert to 'a' node
			Pos_t newPos = a->ElemCount + pos; // Can be 0
			DATAMOVE(a->Elems + a->ElemCount, b->Elems, pos, a, b);
			a->Elems[newPos] = newElem;
			DATAMOVE(a->Elems + newPos + 1, b->Elems + pos,  num - 1 - pos, a, b);
			if (!MoveAll)
				DATAMOVE(b->Elems, b->Elems + num - 1, b->ElemCount - num + 1, b, b);
		}

		a->ElemCount += num;
		b->ElemCount -= (num - 1);
		*aExt.maxElemCopy = a->LastElem();
		if (!MoveAll)
			*bExt.maxElemCopy = b->LastElem();
		count++;
		return true;
	}

	template<bool MoveToEmpty, bool MoveAll, class Node_t>
	bool moveLeftInsert(CNodeExtended<CInner> &aExt, CNodeExtended<CInner> &bExt, Pos_t num, const CNodeExtended<Node_t> &newExt)
	{
		CInner *a = aExt.node;
		CInner *b = bExt.node;
		Pos_t pos = newExt.posInParent;

		btree_assert(num > 0);
		btree_assert(b->ElemCount >= num - 1);
		btree_assert(a->ElemCount + num <= a->ElemMaxCount);
		btree_assert(pos >= 0);
		btree_assert(pos <= b->ElemCount);
		btree_assert(MoveToEmpty == (a->ElemCount == 0));
		btree_assert(MoveAll == (b->ElemCount == num - 1));

		if (pos >= num) {
			// In fact insert to 'b' node
			Pos_t newPos = pos - num; // Can be 0
			DATAMOVE(a->ChildIds + a->ElemCount, b->ChildIds, num, a, b);
			DATAMOVE(b->ChildIds, b->ChildIds + num, newPos, b, b);
			b->ChildIds[newPos] = newExt.nodeId;
			DATAMOVE(b->ChildIds + newPos + 1, b->ChildIds + pos, b->ElemCount - pos, b, b);

			if (!MoveToEmpty)
				a->Elems[a->ElemCount - 1] = *aExt.maxElemCopy;

			DATAMOVE(a->Elems + a->ElemCount, b->Elems, num - 1, a, b);
			if (num < b->ElemCount)
				*aExt.maxElemCopy = b->Elems[num - 1];
			else
				*aExt.maxElemCopy = *bExt.maxElemCopy;

			if (pos == b->ElemCount) { // arrow is righter than star
				if (num < b->ElemCount) {
					DATAMOVE(b->Elems, b->Elems + num, b->ElemCount - num - 1, b, b);
					b->Elems[b->ElemCount - num - 1] = *bExt.maxElemCopy;
				}
				*bExt.maxElemCopy = *newExt.maxElemCopy;
			} else { // star is righter than arrow
				DATAMOVE(b->Elems, b->Elems + num, newPos, b, b);
				b->Elems[newPos] = *newExt.maxElemCopy;
				DATAMOVE(b->Elems + newPos + 1, b->Elems + pos, b->ElemCount - pos - 1, b, b);
			}
		} else {
			// In fact insert to 'a' node
			Pos_t newPos = a->ElemCount + pos; // Can be 0
			DATAMOVE(a->ChildIds + a->ElemCount, b->ChildIds, pos, a, b);
			a->ChildIds[newPos] = newExt.nodeId;
			DATAMOVE(a->ChildIds + newPos + 1, b->ChildIds + pos,  num - 1 - pos, a, b);
			if (!MoveAll)
				DATAMOVE(b->ChildIds, b->ChildIds + num - 1, b->ElemCount - num + 1, b, b);

			if (!MoveToEmpty)
				a->Elems[a->ElemCount - 1] = *aExt.maxElemCopy;

			if (!MoveAll) {
				DATAMOVE(a->Elems + a->ElemCount, b->Elems, pos, a, b);
			} else {
				if (pos == b->ElemCount) {
					if (pos > 0) { // why?
						DATAMOVE(a->Elems + a->ElemCount, b->Elems, pos - 1, a, b);
						a->Elems[newPos - 1] = *bExt.maxElemCopy;
					}
				} else {
					DATAMOVE(a->Elems + a->ElemCount, b->Elems, pos, a, b);
				}
			}
			if (newPos ==  a->ElemCount + num - 1) {
				*aExt.maxElemCopy = *newExt.maxElemCopy;
			} else {
				a->Elems[newPos] = *newExt.maxElemCopy;
				DATAMOVE(a->Elems + newPos + 1, b->Elems + pos,  num - 1 - pos - 1, a, b);
				if (MoveAll)
					*aExt.maxElemCopy = *bExt.maxElemCopy;
				else
					*aExt.maxElemCopy = b->Elems[num - 2];
			}
			if (!MoveAll)
				DATAMOVE(b->Elems, b->Elems + num - 1, b->ElemCount - num, b, b);
		}

		a->ElemCount += num;
		b->ElemCount -= (num - 1);
		return true;
	}
	
	// Simple case or replacing
	bool processReplace(CNodeExtended<CLeaf> &ext, const Elem_t &newElem, Elem_t *replaced)
	{
		CLeaf *a = ext.node;
		btree_assert(ext.insertionPoint < a->ElemCount);

		if (replaced)
			*replaced = a->Elems[ext.insertionPoint];
		a->Elems[ext.insertionPoint] = newElem;
		*ext.maxElemCopy = a->Elems[a->ElemCount - 1];
		return true;
	}

	// Simple case of first time inserting
	bool insertFirstElem(const Elem_t& newElem, bool allowInsert)
	{
		btree_assert(depth == 0);
		btree_assert(count == 0);
		btree_assert(leafCount == 0);
		if (!allowInsert)
			return false;
		maxElem = newElem;
		CLeaf *leaf = createNode<CLeaf>(rootId);
		leaf->ElemCount = 1;
		leaf->Elems[0] = newElem;
		root = leaf;
		depth = 1;
		count = 1;
		return true;
	}

	// Insert worker
	template<class Node_t, class What_t>
	bool processInsert(CNodeExtended<Node_t> &ext, const What_t &newElem)
	{
		if (ext.node->SpareCount())
			return simpleInsert(ext, newElem);
		CNodeExtended<Node_t> leftExt, rightExt, leftLeftExt, rightRightExt;
		bool hasLeftExt = collectLeftExt(ext, leftExt);
		bool hasRightExt = collectRightExt(ext, rightExt);
		bool hasLeftLeftExt = false;
		bool hasRightRightExt = false;
		if (hasLeftExt && hasRightExt) {
			if (leftExt.node->SpareCount() > rightExt.node->SpareCount()) {
				Pos_t moveCount = 1 + leftExt.node->SpareCount() / 2;
				return moveLeftInsert<false, false>(leftExt, ext, moveCount, newElem);
			} else if (rightExt.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + rightExt.node->SpareCount() / 2;
				return moveRightInsert<false, false>(ext, rightExt, moveCount, newElem);
			}
		} else if (hasLeftExt) {
			if (leftExt.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + leftExt.node->SpareCount() / 2;
				return moveLeftInsert<false, false>(leftExt, ext, moveCount, newElem);
			}
			hasLeftLeftExt = collectLeftExt(leftExt, leftLeftExt);
			if (hasLeftLeftExt && leftLeftExt.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + (2 * leftLeftExt.node->SpareCount() - 1) / 3;
				moveLeft<false, false>(leftLeftExt, leftExt, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveLeftInsert<false, false>(leftExt, ext, moveCount, newElem);
			}
		} else if (hasRightExt) {
			if (rightExt.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + rightExt.node->SpareCount() / 2;
				return moveRightInsert<false, false>(ext, rightExt, moveCount, newElem);
			}
			hasRightRightExt = collectRightExt(rightExt, rightRightExt);
			if (hasRightRightExt && rightRightExt.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + (2 * rightRightExt.node->SpareCount() - 1) / 3;
				moveRight<false, false>(rightExt, rightRightExt, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveRightInsert<false, false>(ext, rightExt, moveCount, newElem);
			}
		}
		NodeId_t newNodeId;
		Node_t *newNode = createNode<Node_t>(newNodeId);
		newNode->ElemCount = 0;
		CNodeExtended<Node_t> newExt;
		Elem_t newMaxElem;
		prepareNewExt(ext, newExt, newNode, newNodeId, &newMaxElem);
		if (hasLeftExt && hasRightExt) {
			Pos_t moveCount = newNode->ElemMaxCount / 4;
			moveRightInsert<true, false>(ext, newExt, moveCount * 2, newElem);
			moveLeft<false, false>(newExt, rightExt, moveCount);
			moveRight<false, false>(leftExt, ext, moveCount);
		} else if (hasLeftExt && hasLeftLeftExt) {
			Pos_t moveCount = newNode->ElemMaxCount / 4;
			moveRightInsert<true, false>(ext, newExt, moveCount * 3, newElem);
			moveRight<false, false>(leftExt, ext, moveCount * 2);
			moveRight<false, false>(leftLeftExt, leftExt, moveCount);
		} else if (hasRightExt && hasRightRightExt) {
			Pos_t moveCount = newNode->ElemMaxCount / 4;
			moveRightInsert<true, false>(ext, newExt, moveCount, newElem);
			moveLeft<false, false>(newExt, rightExt, moveCount * 2);
			moveLeft<false, false>(rightExt, rightRightExt, moveCount);
		} else if (hasLeftExt) {
			Pos_t moveCount = newNode->ElemMaxCount / 3;
			moveRightInsert<true, false>(ext, newExt, moveCount * 2, newElem);
			moveRight<false, false>(leftExt, ext, moveCount);
		} else if (hasRightExt) {
			Pos_t moveCount = newNode->ElemMaxCount / 3;
			moveRightInsert<true, false>(ext, newExt, moveCount, newElem);
			moveLeft<false, false>(newExt, rightExt, moveCount);
		} else {
			btree_assert(ext.parentInfo == 0);
			Pos_t moveCount = newNode->ElemMaxCount / 2;
			moveRightInsert<true, false>(ext, newExt, moveCount, newElem);

			NodeId_t newRootId;
			CInner *newRoot = createNode<CInner>(newRootId);
			newRoot->ElemCount = 2;
			newRoot->ChildIds[0] = rootId;
			newRoot->ChildIds[1] = newNodeId;
			newRoot->Elems[0] = maxElem;
			root = newRoot;
			rootId = newRootId;
			maxElem = newMaxElem;
			depth++;
			return true;
		}
		btree_assert(ext.parentInfo);
		return processInsert(*ext.parentInfo, newExt);
	}

	// Delete worker
	template<class Node_t>
	bool processDelete(CNodeExtended<Node_t> &ext)
	{
		simpleDelete(ext);

		if (ext.node->ElemCount >= ext.node->ElemMaxCount * 2 / 3)
			return true;

		CNodeExtended<Node_t> leftExt, rightExt, leftLeftExt, rightRightExt;
		bool hasLeftExt = collectLeftExt(ext, leftExt);
		bool hasRightExt = collectRightExt(ext, rightExt);
		bool hasLeftLeftExt = false;
		bool hasRightRightExt = false;
		if (hasLeftExt && hasRightExt) {
			if (leftExt.node->OverCount() > rightExt.node->OverCount()) {
				Pos_t moveCount = 1 + leftExt.node->OverCount() / 2;
				return moveRight<false, false>(leftExt, ext, moveCount);
			} else if (rightExt.node->OverCount() > 0) {
				Pos_t moveCount = 1 + rightExt.node->OverCount() / 2;
				return moveLeft<false, false>(ext, rightExt, moveCount);
			}
		} else if (hasLeftExt) {
			if (leftExt.node->OverCount() > 0) {
				Pos_t moveCount = 1 + leftExt.node->OverCount() / 2;
				return moveRight<false, false>(leftExt, ext, moveCount);
			}
			hasLeftLeftExt = collectLeftExt(leftExt, leftLeftExt);
			if (hasLeftLeftExt && leftLeftExt.node->OverCount() > 0) {
				Pos_t moveCount = 1 + (2 * leftLeftExt.node->OverCount() - 1) / 3;
				moveRight<false, false>(leftExt, ext, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveRight<false, false>(leftLeftExt, leftExt, moveCount);
			}
		} else if (hasRightExt) {
			if (rightExt.node->OverCount() > 0) {
				Pos_t moveCount = 1 + rightExt.node->OverCount() / 2;
				return moveLeft<false, false>(ext, rightExt, moveCount);
			}
			hasRightRightExt = collectRightExt(rightExt, rightRightExt);
			if (hasRightRightExt && rightRightExt.node->OverCount() > 0) {
				Pos_t moveCount = 1 + (2 * rightRightExt.node->OverCount() - 1) / 3;
				moveLeft<false, false>(ext, rightExt, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveLeft<false, false>(rightExt, rightRightExt, moveCount);
			}
		}

		if (hasLeftExt && hasRightExt) {
			Pos_t moveCount = (ext.node->ElemCount + 1) / 2;
			moveRight<false, false>(ext, rightExt, moveCount);
			moveCount = ext.node->ElemCount;
			moveLeft<false, true>(leftExt, ext, moveCount);
		} else if (hasLeftExt && hasLeftLeftExt) {
			Pos_t moveCount = (ext.node->ElemCount + 1) / 2;
			moveLeft<false, false>(leftLeftExt, leftExt, moveCount);
			moveCount = ext.node->ElemCount;
			moveLeft<false, true>(leftExt, ext, moveCount);
		} else if (hasRightExt && hasRightRightExt) {
			Pos_t moveCount = (ext.node->ElemCount + 1) / 2;
			moveRight<false, false>(rightExt, rightRightExt, moveCount);
			moveCount = ext.node->ElemCount;
			moveRight<false, true>(ext, rightExt, moveCount);
		} else if (hasLeftExt) {
			if (ext.node->ElemCount + leftExt.node->ElemCount > ext.node->ElemMaxCount)
				return true;
			Pos_t moveCount = ext.node->ElemCount;
			moveLeft<false, true>(leftExt, ext, moveCount);
		} else if (hasRightExt) {
			if (ext.node->ElemCount + rightExt.node->ElemCount > ext.node->ElemMaxCount)
				return true;
			Pos_t moveCount = ext.node->ElemCount;
			moveRight<false, true>(ext, rightExt, moveCount);
		} else {
			if (CA_TYPEIS(Node_t, CLeaf)) {
				if (ext.node->ElemCount > 0)
					return true;
				btree_assert(ext.parentInfo == 0);
				btree_assert(depth == 1);
				btree_assert(count == 0);
				root = 0;
				depth = 0;
				disposeNode(ext.node, ext.nodeId);
				return true;
			} else {
				if (ext.node->ElemCount > 1)
					return true;
				btree_assert(depth > 1);
				btree_assert(ext.parentInfo == 0);
				depth--;
				rootId = getFirstNodeChild(ext.node);
				root = getNode(rootId);
				disposeNode(ext.node, ext.nodeId);
				return true;
			}
		}
		btree_assert(ext.node->ElemCount ==  0);
		disposeNode(ext.node, ext.nodeId);
		btree_assert(ext.parentInfo);
		return processDelete(*ext.parentInfo);
	}

// Node allocation and node extracting
	void garbagePush(void *buf, NodeId_t id)
	{
		garbageCount++;
		CGarbage *node = new(buf) CGarbage;
		node->Id = id;
		node->Next = garbage;
		garbage = node;
	}
	void *garbagePop(NodeId_t &id)
	{
		garbageCount--;
		CGarbage *node = garbage;
		id = node->Id;
		garbage = garbage->Next;
		node->~CGarbage();
		return static_cast<void *>(node);
	}

	template<class Node_t>
	Node_t *createNode(NodeId_t &id)
	{
		CT_ASSERT_TYPEISOR(Node_t, CLeaf, CInner);
		if (CA_TYPEIS(Node_t, CLeaf))
			leafCount++;
		else
			internalCount++;
		void *buf;
		if (garbage)
			buf = garbagePop(id);
		else
			buf = providence.Alloc(id);
		return new(buf) Node_t;
	}
	template<class Node_t>
	void disposeNode(Node_t *node, NodeId_t id)
	{
		CT_ASSERT_TYPEISOR(Node_t, CLeaf, CInner);
		if (CA_TYPEIS(Node_t, CLeaf))
			leafCount--;
		else
			internalCount--;
		node->~Node_t();
		garbagePush(static_cast<void *>(node), id);
	}

	CNode *getNode(NodeId_t id) const
	{
		return static_cast<CNode *>(providence.Get(id));
	}
	template<class Node_t>
	Node_t *getNode(NodeId_t id) const
	{
		return static_cast<Node_t *>(providence.Get(id));
	}

	NodeId_t getFirstNodeChild(CLeaf *node)
	{
		btree_assert(false);
		(void)node;
		return (NodeId_t)(-1);
	}

	NodeId_t getFirstNodeChild(CInner *node)
	{
		return node->ChildIds[0];
	}

// Search in node
	template<class CKey>
	Elem_t *findInsPoint(Elem_t *first, Elem_t *end, CKey &key, bool &exact)
	{
		exact = false;
		while (first != end) {
			Elem_t *mid = first + (end - first) / 2;
			long compRes = CComp::Comp(*mid, key, param);
			if (compRes > 0) {
				end = mid;
			} else if (compRes < 0) {
				first = mid + 1;
			} else {
				exact = true;
				return mid;
			}
		}
		return end;
	}
	template<class Node_t, class CKey>
	Pos_t findInsPoint(Node_t *node, CKey &key, bool &exact)
	{
		Elem_t *last = node->Elems + node->ElemCount - (CA_TYPEIS(Node_t, CInner) ? 1 : 0);
		Elem_t *elem = findInsPoint(node->Elems, last, key, exact);
		return static_cast<Pos_t>(elem - node->Elems);
	}

// Path and extended node coolecting
	void collectPath(const Elem_t &newElem, CNodeExtended<CInner> *pathInfo, CNodeExtended<CLeaf> &lastInfo, bool &exact)
	{
		exact = false;

		CNodeExtended<CInner> *prevInfo = 0;
		Pos_t prevPos = 0;
		CNode *node = root;
		NodeId_t nodeId = rootId;
		Elem_t *maxElemCopy = &maxElem;
		for (NodeId_t i = 0; i < depth - 1; i++) {
			CInner *internal = static_cast<CInner *>(node);
			pathInfo[i].node = internal;
			pathInfo[i].nodeId = nodeId;
			pathInfo[i].parentInfo = prevInfo;
			pathInfo[i].posInParent = prevPos;
			pathInfo[i].maxElemCopy = maxElemCopy;
			Pos_t pos;
			if (exact)
				pos = internal->ElemCount - 1;
			else
				pos = findInsPoint(internal, newElem, exact);
			pathInfo[i].insertionPoint = pos;

			if (pos < internal->ElemCount - 1)
				maxElemCopy = internal->Elems + pos;
			nodeId = internal->ChildIds[pos];
			node = getNode(nodeId);
			prevPos = pos;
			prevInfo = pathInfo + i;
		}

		CLeaf *leaf = static_cast<CLeaf *>(node);
		lastInfo.node = leaf;
		lastInfo.nodeId = nodeId;
		lastInfo.parentInfo = prevInfo;
		lastInfo.posInParent = prevPos;
		lastInfo.maxElemCopy = maxElemCopy;
		Pos_t pos;
		if (exact)
			pos = leaf->ElemCount - 1;
		else
			pos = findInsPoint(leaf, newElem, exact);
		lastInfo.insertionPoint = pos;
	}
	template<class Node_t>
	bool collectLeftExt(const CNodeExtended<Node_t> &ext, CNodeExtended<Node_t> &newExt)
	{
		CNodeExtended<CInner> *parentInfo = ext.parentInfo;
		if (!parentInfo)
			return false;
		if (ext.posInParent == 0)
			return false;

		newExt.parentInfo = ext.parentInfo;
		newExt.posInParent = ext.posInParent - 1;
		newExt.nodeId = parentInfo->node->ChildIds[newExt.posInParent];
		newExt.node = getNode<Node_t>(newExt.nodeId);
		newExt.maxElemCopy = parentInfo->node->Elems + newExt.posInParent;
		newExt.insertionPoint = Pos_t(-1); // unused
		return true;
	}
	template<class Node_t>
	bool collectRightExt(const CNodeExtended<Node_t> &ext, CNodeExtended<Node_t> &newExt)
	{
		CNodeExtended<CInner> *parentInfo = ext.parentInfo;
		if (!parentInfo)
			return false;
		if (ext.posInParent >= parentInfo->node->ElemCount - 1)
			return false;

		newExt.parentInfo = ext.parentInfo;
		newExt.posInParent = ext.posInParent + 1;
		newExt.nodeId = parentInfo->node->ChildIds[newExt.posInParent];
		newExt.node = getNode<Node_t>(newExt.nodeId);
		if (newExt.posInParent >= parentInfo->node->ElemCount - 1)
			newExt.maxElemCopy = parentInfo->maxElemCopy;
		else
			newExt.maxElemCopy = parentInfo->node->Elems + newExt.posInParent;
		newExt.insertionPoint = Pos_t(-1); // unused
		return true;
	}
	template<class Node_t>
	void prepareNewExt(const CNodeExtended<Node_t> &info, CNodeExtended<Node_t> &newExt, Node_t* newNode, NodeId_t newId, Elem_t *maxElem)
	{
		CNodeExtended<CInner> *parentInfo = info.parentInfo;

		newExt.parentInfo = parentInfo;
		newExt.posInParent = info.posInParent + 1;
		newExt.nodeId = newId;
		newExt.node = newNode;
		newExt.maxElemCopy = maxElem;
		newExt.insertionPoint = Pos_t(-1); // unused
	}

// Debug tools checking types and buffer overflow
	template<class Range_t, class Data_t>
	struct CRangeChecker {
		static void CheckInRange(const Data_t *data, const Range_t *begin, const Range_t *end)
		{
			// Wrong type. Can I safely use CT_ASSERT(false) here?
			(void)data; (void)begin; (void)end;
			btree_assert(false);
		}
	};
	template<class Range_t>
	struct CRangeChecker<Range_t, Range_t> {
		static void CheckInRange(const Range_t *data, const Range_t *begin, const Range_t *end)
		{
			(void)data; (void)begin; (void)end;
			btree_assert(data >= begin && data < end);
		}
	};

	template<class Range1_t, class Range2_t, class Data_t>
	struct CRangesChecker {
		static void CheckInRanges(const Data_t *data, const Range1_t *begin1, const Range1_t *end1, const Range2_t *begin2, const Range2_t *end2)
		{
			// Wrong type. Can I safely use CT_ASSERT(false) here?
			(void)data; (void)begin1; (void)end1; (void)begin2; (void)end2;
			btree_assert(false);
		}
	};
	template<class Range_t>
	struct CRangesChecker<Range_t, Range_t, Range_t> {
		static void CheckInRanges(const Range_t *data, const Range_t *begin1, const Range_t *end1, const Range_t *begin2, const Range_t *end2)
		{
			(void)data; (void)begin1; (void)end1; (void)begin2; (void)end2;
			btree_assert((data >= begin1 && data < end1) || (data >= begin2 && data < end2));
		}
	};
	template<class Range_t, class Data_t>
	static void checkInRange(const Data_t *data, const Range_t *begin, const Range_t *end)
	{
		CRangeChecker<Range_t, Data_t>::CheckInRange(data, begin, end);
	}
	template<class Range1_t, class Range2_t, class Data_t>
	static void checkInRanges(const Data_t *data, const Range1_t *begin1, const Range1_t *end1, const Range2_t *begin2, const Range2_t *end2)
	{
		CRangesChecker<Range1_t, Range2_t, Data_t>::CheckInRanges(data, begin1, end1, begin2, end2);
	}


	template<class Data_t, class Node_t>
	struct CDataMoveChecker {
		static void Check(Data_t *dst, const Data_t *src, size_t num, Node_t *dstNode, Node_t *srcNode)
		{
			// Wrong type. Can I safely use CT_ASSERT(false) here?
			(void)dst; (void)src; (void)num; (void)dstNode; (void)srcNode;
			btree_assert(false);
		}
	};
	template<class Data_t>
	struct CDataMoveChecker<Data_t, CLeaf> {
		typedef CLeaf Node_t;
		static void Check(Data_t *dst, const Data_t *src, size_t num, Node_t *dstNode, Node_t *srcNode)
		{
			(void)dst; (void)src; (void)num; (void)dstNode; (void)srcNode;

			if (!CA_TYPEIS(Data_t, Elem_t))
				btree_assert(false);

			if (num > 0) {
				checkInRange(src, srcNode->Elems, srcNode->Elems + srcNode->ElemMaxCount);
				checkInRange(src + num - 1, srcNode->Elems, srcNode->Elems + srcNode->ElemMaxCount);
				checkInRange(dst, dstNode->Elems, dstNode->Elems + dstNode->ElemMaxCount);
				checkInRange(dst + num - 1, dstNode->Elems, dstNode->Elems + dstNode->ElemMaxCount);
			} else {
				checkInRange(src, srcNode->Elems, srcNode->Elems + srcNode->ElemMaxCount + 1);
				checkInRange(dst, dstNode->Elems, dstNode->Elems + dstNode->ElemMaxCount + 1);
			}
		}
	};
	template<class Data_t>
	struct CDataMoveChecker<Data_t, CInner> {
		typedef CInner Node_t;
		static void Check(Data_t *dst, const Data_t *src, size_t num, Node_t *dstNode, Node_t *srcNode)
		{
			(void)dst; (void)src; (void)num; (void)dstNode; (void)srcNode;

			if (num > 0) {
				if (CA_TYPEIS(Data_t, Elem_t) && CA_TYPEIS(Data_t, NodeId_t)) {
					checkInRanges(src, srcNode->Elems, srcNode->Elems + srcNode->ElemMaxCount - 1,
							srcNode->ChildIds, srcNode->ChildIds + srcNode->ElemMaxCount);
					checkInRanges(src + num - 1, srcNode->Elems, srcNode->Elems + srcNode->ElemMaxCount - 1,
							srcNode->ChildIds, srcNode->ChildIds + srcNode->ElemMaxCount);
					checkInRanges(dst, dstNode->Elems, dstNode->Elems + dstNode->ElemMaxCount - 1,
							dstNode->ChildIds, dstNode->ChildIds + dstNode->ElemMaxCount);
					checkInRanges(dst + num - 1, dstNode->Elems, dstNode->Elems + dstNode->ElemMaxCount - 1,
							dstNode->ChildIds, dstNode->ChildIds + dstNode->ElemMaxCount);
				} else if (CA_TYPEIS(Data_t, Elem_t)) {
					checkInRange(src, srcNode->Elems, srcNode->Elems + srcNode->ElemMaxCount - 1);
					checkInRange(src + num - 1, srcNode->Elems, srcNode->Elems + srcNode->ElemMaxCount - 1);
					checkInRange(dst, dstNode->Elems, dstNode->Elems + dstNode->ElemMaxCount - 1);
					checkInRange(dst + num - 1, dstNode->Elems, dstNode->Elems + dstNode->ElemMaxCount - 1);
				} else if (CA_TYPEIS(Data_t, NodeId_t)) {
					checkInRange(src, srcNode->ChildIds, srcNode->ChildIds + srcNode->ElemMaxCount);
					checkInRange(src + num - 1, srcNode->ChildIds, srcNode->ChildIds + srcNode->ElemMaxCount);
					checkInRange(dst, dstNode->ChildIds, dstNode->ChildIds + dstNode->ElemMaxCount);
					checkInRange(dst + num - 1, dstNode->ChildIds, dstNode->ChildIds + dstNode->ElemMaxCount);
				} else {
					btree_assert(false);
				}
			} else {
				if (CA_TYPEIS(Data_t, Elem_t) && CA_TYPEIS(Data_t, NodeId_t)) {
					checkInRanges(src, srcNode->Elems, srcNode->Elems + srcNode->ElemMaxCount,
							srcNode->ChildIds, srcNode->ChildIds + srcNode->ElemMaxCount + 1);
					checkInRanges(dst, dstNode->Elems, dstNode->Elems + dstNode->ElemMaxCount,
							dstNode->ChildIds, dstNode->ChildIds + dstNode->ElemMaxCount + 1);
				} else if (CA_TYPEIS(Data_t, Elem_t)) {
					checkInRange(src, srcNode->Elems, srcNode->Elems + srcNode->ElemMaxCount);
					checkInRange(dst, dstNode->Elems, dstNode->Elems + dstNode->ElemMaxCount);
				} else if (CA_TYPEIS(Data_t, NodeId_t)) {
					checkInRange(src, srcNode->ChildIds, srcNode->ChildIds + srcNode->ElemMaxCount + 1);
					checkInRange(dst, dstNode->ChildIds, dstNode->ChildIds + dstNode->ElemMaxCount + 1);
				} else {
					btree_assert(false);
				}
			}
		}
	};
	
	// Debug version of data moving
	template<class Data_t, class Node_t>
	void debugDataMove(Data_t *dst, const Data_t *src, size_t num, Node_t *dstNode, Node_t *srcNode)
	{
		(void)dstNode; (void)srcNode;

		CT_ASSERT_TYPEISOR(Node_t, CLeaf, CInner);

		CDataMoveChecker<Data_t, Node_t>::Check(dst, src, num, dstNode, srcNode);

		memmove(dst, src, num * sizeof(dst[0]));
	}
// Debug tools checking types and buffer overflow END


// Debug utilities for testing base operation on nodes: inserting, deleting, moving to left and right nodes, and (inserting and moving)
public:
	static int InternalMechanismCheck(bool assertme)
	{
		This_t test;
		return test.internalMechanismCheck(assertme);
	}

private:
	static void debugSetElem(Elem_t &elem, unsigned char c)
	{
		memset(&elem, 0, sizeof(Elem_t));
		*(unsigned char *)&elem = c;
	}

	static unsigned char debugGetElem(const Elem_t &elem)
	{
		return *(unsigned char *)&elem;
	}

	static void debugSetElem(CNodeExtended<CInner> &ext, Pos_t pos, unsigned char c)
	{
		btree_assert(pos >= 0);
		btree_assert(pos < ext.node->ElemCount);
		if (pos < ext.node->ElemCount - 1)
			debugSetElem(ext.node->Elems[pos], c);
		else
			debugSetElem(*ext.maxElemCopy, c);
	}

	static unsigned char debugGetElem(const CNodeExtended<CInner> &ext, Pos_t pos)
	{
		btree_assert(pos >= 0);
		btree_assert(pos < ext.node->ElemCount);
		if (pos < ext.node->ElemCount - 1)
			return *(unsigned char *)(ext.node->Elems + pos);
		else
			return *(unsigned char *)(ext.maxElemCopy);
	}

	int internalMechanismCheckInsertLeaf(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i < CLeaf::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= i; j++) {
				count = 0;
				CLeaf node;
				node.ElemCount = i;
				for (unsigned int k = 0; k < CLeaf::ElemMaxCount; k++)
					if (k < j)
						debugSetElem(node.Elems[k], k);
					else
						debugSetElem(node.Elems[k], k + 1);
				CNodeExtended<CLeaf> ext;
				Elem_t max, ins;
				debugSetElem(max, i + 1);
				debugSetElem(ins, j);
				ext.node = &node;
				ext.insertionPoint = j;
				ext.maxElemCopy = &max;
				if (!simpleInsert(ext, ins)) {
					result |= (1 << 0);
					btree_assert(!assertme);
				}

				if (node.ElemCount != Pos_t(i + 1) || count != Pos_t(1)) {
					result |= (1 << 0);
					btree_assert(!assertme);
				}
				if (debugGetElem(max) != debugGetElem(node.LastElem())) {
					result |= (1 << 1);
					btree_assert(!assertme);
				}
				for (unsigned int k = 0; k <= i; k++) {
					if (debugGetElem(node.Elems[k]) != (unsigned char)k) {
						result |= (1 << 1);
						btree_assert(!assertme);
					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckDeleteLeaf(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 1; i <= CLeaf::ElemMaxCount; i++) {
			for (unsigned int j = 0; j < i; j++) {
				count = 1;
				CLeaf node;
				node.ElemCount = i;
				for (unsigned int k = 0; k < CLeaf::ElemMaxCount; k++)
					debugSetElem(node.Elems[k], k);
				CNodeExtended<CLeaf> ext;
				Elem_t max;
				debugSetElem(max, j == i - 1 ? i - 2 : i - 1);
				ext.node = &node;
				ext.insertionPoint = j;
				ext.maxElemCopy = &max;
				if (!simpleDelete(ext)) {
					result |= (1 << 2);
					btree_assert(!assertme);
				}

				if (node.ElemCount != Pos_t(i - 1) || count != Pos_t(0)) {
					result |= (1 << 2);
					btree_assert(!assertme);
				}
				if (i > 1 && debugGetElem(max) != debugGetElem(node.LastElem())) {
					result |= (1 << 3);
					btree_assert(!assertme);
				}
				for (unsigned int k = 0; k < i - 1; k++) {
					if (debugGetElem(node.Elems[k]) != (unsigned char)( k < j ? k : k + 1)) {
						result |= (1 << 3);
						btree_assert(!assertme);
					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckMoveRightLeaf(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i <= CLeaf::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= CLeaf::ElemMaxCount; j++) {
				unsigned int maxMove = i < CLeaf::ElemMaxCount - j ? i : CLeaf::ElemMaxCount - j;
				for (unsigned int k = 1; k <= maxMove; k++) {
					CLeaf a, b;
					memset(a.Elems, 0xFF, sizeof(a.Elems));
					memset(b.Elems, 0xFF, sizeof(b.Elems));
					a.ElemCount = i;
					b.ElemCount = j;
					unsigned char c = 0;
					for (unsigned int u = 0; u < i; u++)
						debugSetElem(a.Elems[u], c++);
					for (unsigned int u = 0; u < j; u++)
						debugSetElem(b.Elems[u], c++);
					Elem_t ma = Elem_t(), mb = Elem_t();
					if (i)
						ma = a.LastElem();
					if (j)
						mb = b.LastElem();

					CNodeExtended<CLeaf> aExt, bExt;
					aExt.node = &a;
					aExt.maxElemCopy = &ma;
					bExt.node = &b;
					bExt.maxElemCopy = &mb;

					if (j) {
						const bool MoveToEmpty = false;
						if (k < i) {
							const bool MoveAll = false;
							moveRight<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						} else {
							const bool MoveAll = true;
							moveRight<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						}
					} else {
						const bool MoveToEmpty = true;
						if (k < i) {
							const bool MoveAll = false;
							moveRight<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						} else {
							const bool MoveAll = true;
							moveRight<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						}
					}

					if (a.ElemCount != (Pos_t)(i - k)) {
						result |= (1 << 4);
						btree_assert(!assertme);
					}
					if (b.ElemCount != (Pos_t)(j + k)) {
						result |= (1 << 4);
						btree_assert(!assertme);
					}

					if (i - k)
						if (ma != a.LastElem()) {
							result |= (1 << 5);
							btree_assert(!assertme);
						}
					if (j + k)
						if (mb != b.LastElem()) {
							result |= (1 << 5);
							btree_assert(!assertme);
						}

					c = 0;
					for (unsigned int u = 0; u < (unsigned int)a.ElemCount; u++)
						if (debugGetElem(a.Elems[u]) != c++) {
							result |= (1 << 5);
							btree_assert(!assertme);
						}
					for (unsigned int u = 0; u < (unsigned int)b.ElemCount; u++)
						if (debugGetElem(b.Elems[u]) != c++) {
							result |= (1 << 5);
							btree_assert(!assertme);
						}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckMoveLeftLeaf(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i <= CLeaf::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= CLeaf::ElemMaxCount; j++) {
				unsigned int maxMove = j < CLeaf::ElemMaxCount - i ? j : CLeaf::ElemMaxCount - i;
				for (unsigned int k = 1; k <= maxMove; k++) {
					CLeaf a, b;
					memset(a.Elems, 0xFF, sizeof(a.Elems));
					memset(b.Elems, 0xFF, sizeof(b.Elems));
					a.ElemCount = i;
					b.ElemCount = j;
					unsigned char c = 0;
					for (unsigned int u = 0; u < i; u++)
						debugSetElem(a.Elems[u], c++);
					for (unsigned int u = 0; u < j; u++)
						debugSetElem(b.Elems[u], c++);
					Elem_t ma = Elem_t(), mb = Elem_t();
					if (i)
						ma = a.LastElem();
					if (j)
						mb = b.LastElem();

					CNodeExtended<CLeaf> aExt, bExt;
					aExt.node = &a;
					aExt.maxElemCopy = &ma;
					bExt.node = &b;
					bExt.maxElemCopy = &mb;

					if (i) {
						const bool MoveToEmpty = false;
						if (k < j) {
							const bool MoveAll = false;
							moveLeft<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						} else {
							const bool MoveAll = true;
							moveLeft<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						}
					} else {
						const bool MoveToEmpty = true;
						if (k < j) {
							const bool MoveAll = false;
							moveLeft<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						} else {
							const bool MoveAll = true;
							moveLeft<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						}
					}

					if (a.ElemCount != (Pos_t)(i + k)) {
						result |= (1 << 6);
						btree_assert(!assertme);
					}
					if (b.ElemCount != (Pos_t)(j - k)) {
						result |= (1 << 6);
						btree_assert(!assertme);
					}

					if (i + k)
						if (ma != a.LastElem()) {
							result |= (1 << 7);
							btree_assert(!assertme);
						}
					if (j - k)
						if (mb != b.LastElem()) {
							result |= (1 << 7);
							btree_assert(!assertme);
						}

					c = 0;
					for (unsigned int u = 0; u < (unsigned int)a.ElemCount; u++)
						if (debugGetElem(a.Elems[u]) != c++) {
							result |= (1 << 7);
							btree_assert(!assertme);
						}
					for (unsigned int u = 0; u < (unsigned int)b.ElemCount; u++)
						if (debugGetElem(b.Elems[u]) != c++) {
							result |= (1 << 7);
							btree_assert(!assertme);
						}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckMoveRightInsertLeaf(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i <= CLeaf::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= CLeaf::ElemMaxCount; j++) {
				unsigned int maxMove = i + 1 < CLeaf::ElemMaxCount - j ? i + 1 : CLeaf::ElemMaxCount - j;
				for (unsigned int k = 0; k <= i; k++) {
					for (unsigned int u = 1; u <= maxMove; u++) {
						CLeaf a, b;
						memset(a.Elems, 0xFF, sizeof(a.Elems));
						memset(b.Elems, 0xFF, sizeof(b.Elems));
						a.ElemCount = i;
						b.ElemCount = j;
						unsigned char c = 0;
						unsigned char ic = i + j;
						for (unsigned int v = 0; v < i; v++) {
							if (v == k)
								ic = c++;
							debugSetElem(a.Elems[v], c++);
						}
						if (k == i)
							ic = c++;
						for (unsigned int v = 0; v < j; v++)
							debugSetElem(b.Elems[v], c++);
						Elem_t ma = Elem_t(), mb = Elem_t();
						if (i)
							ma = a.LastElem();
						if (j)
							mb = b.LastElem();

						CNodeExtended<CLeaf> aExt, bExt;
						aExt.node = &a;
						aExt.maxElemCopy = &ma;
						bExt.node = &b;
						bExt.maxElemCopy = &mb;
						aExt.insertionPoint = k;
						Elem_t ins;
						debugSetElem(ins, ic);

						if (j) {
							const bool MoveToEmpty = false;
							if (u < i + 1) {
								const bool MoveAll = false;
								moveRightInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, ins);
							} else {
								const bool MoveAll = true;
								moveRightInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, ins);
							}
						} else {
							const bool MoveToEmpty = true;
							if (u < i + 1) {
								const bool MoveAll = false;
								moveRightInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, ins);
							} else {
								const bool MoveAll = true;
								moveRightInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, ins);
							}
						}

						if (a.ElemCount != (Pos_t)(i - u + 1)) {
							result |= (1 << 8);
							btree_assert(!assertme);
						}
						if (b.ElemCount != (Pos_t)(j + u)) {
							result |= (1 << 8);
							btree_assert(!assertme);
						}

						if (i - u + 1)
							if (ma != a.LastElem()) {
								result |= (1 << 9);
								btree_assert(!assertme);
							}
						if (j + u)
							if (mb != b.LastElem()) {
								result |= (1 << 9);
								btree_assert(!assertme);
							}

						c = 0;
						for (unsigned int v = 0; v < (unsigned int)a.ElemCount; v++)
							if (debugGetElem(a.Elems[v]) != c++) {
								result |= (1 << 9);
								btree_assert(!assertme);
							}
						for (unsigned int v = 0; v < (unsigned int)b.ElemCount; v++)
							if (debugGetElem(b.Elems[v]) != c++) {
								result |= (1 << 9);
								btree_assert(!assertme);
							}


					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckMoveLeftInsertLeaf(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i <= CLeaf::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= CLeaf::ElemMaxCount; j++) {
				unsigned int maxMove = j + 1 < CLeaf::ElemMaxCount - i ? j + 1 : CLeaf::ElemMaxCount - i;
				for (unsigned int k = 0; k <= j; k++) {
					for (unsigned int u = 1; u <= maxMove; u++) {
						CLeaf a, b;
						memset(a.Elems, 0xFF, sizeof(a.Elems));
						memset(b.Elems, 0xFF, sizeof(b.Elems));
						a.ElemCount = i;
						b.ElemCount = j;
						unsigned char c = 0;
						unsigned char ic = i + j;
						for (unsigned int v = 0; v < i; v++)
							debugSetElem(a.Elems[v], c++);
						for (unsigned int v = 0; v < j; v++) {
							if (v == k)
								ic = c++;
							debugSetElem(b.Elems[v], c++);
						}
						Elem_t ma = Elem_t(), mb = Elem_t();
						if (i)
							ma = a.LastElem();
						if (j)
							mb = b.LastElem();

						CNodeExtended<CLeaf> aExt, bExt;
						aExt.node = &a;
						aExt.maxElemCopy = &ma;
						bExt.node = &b;
						bExt.maxElemCopy = &mb;
						bExt.insertionPoint = k;
						Elem_t ins;
						debugSetElem(ins, ic);

						if (i) {
							const bool MoveToEmpty = false;
							if (u < j + 1) {
								const bool MoveAll = false;
								moveLeftInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, ins);
							} else {
								const bool MoveAll = true;
								moveLeftInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, ins);
							}
						} else {
							const bool MoveToEmpty = true;
							if (u < j + 1) {
								const bool MoveAll = false;
								moveLeftInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, ins);
							} else {
								const bool MoveAll = true;
								moveLeftInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, ins);
							}
						}

						if (a.ElemCount != (Pos_t)(i + u)) {
							result |= (1 << 10);
							btree_assert(!assertme);
						}
						if (b.ElemCount != (Pos_t)(j - u + 1)) {
							result |= (1 << 10);
							btree_assert(!assertme);
						}

						if (i + u)
							if (ma != a.LastElem()) {
								result |= (1 << 11);
								btree_assert(!assertme);
							}
						if (j - u + 1)
							if (mb != b.LastElem()) {
								result |= (1 << 11);
								btree_assert(!assertme);
							}

						c = 0;
						for (unsigned int v = 0; v < (unsigned int)a.ElemCount; v++)
							if (debugGetElem(a.Elems[v]) != c++) {
								result |= (1 << 11);
								btree_assert(!assertme);
							}
						for (unsigned int v = 0; v < (unsigned int)b.ElemCount; v++)
							if (debugGetElem(b.Elems[v]) != c++) {
								result |= (1 << 11);
								btree_assert(!assertme);
							}


					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckInsertInner(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i < CInner::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= i; j++) {
				count = 0;

				CInner node;
				memset(node.Elems, 0xFF, sizeof(node.Elems));
				memset(node.ChildIds, 0xFF, sizeof(node.ChildIds));
				node.ElemCount = i;

				Elem_t max, ins;
				debugSetElem(ins, j);

				CNodeExtended<CInner> ext;
				ext.node = &node;
				ext.maxElemCopy = &max;

				for (unsigned int k = 0; k < i; k++) {
					if (k < j)
						debugSetElem(ext, k, k);
					else
						debugSetElem(ext, k, k + 1);
				}
				for (unsigned int k = 0; k < i; k++)
					if (k < j)
						node.ChildIds[k] = (NodeId_t)k;
					else
						node.ChildIds[k] = (NodeId_t)(k + 1);

				CNodeExtended<CInner> newExt;
				newExt.maxElemCopy = &ins;
				newExt.nodeId = j;
				newExt.posInParent = j;

				if (!simpleInsert(ext, newExt)) {
					result |= (1 << 12);
					btree_assert(!assertme);
				}

				for (unsigned int k = 0; k <= i; k++) {
					if (debugGetElem(ext, k) != (unsigned char)k) {
						result |= (1 << 13);
						btree_assert(!assertme);
					}
				}
				for (unsigned int k = 0; k <= i; k++) {
					if (node.ChildIds[k] != k) {
						result |= (1 << 13);
						btree_assert(!assertme);
					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckDeleteInner(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 1; i <= CInner::ElemMaxCount; i++) {
			for (unsigned int j = 0; j < i; j++) {
				CInner node;
				node.ElemCount = i;
				for (unsigned int k = 0; k < CInner::ElemMaxCount - 1; k++)
					debugSetElem(node.Elems[k], k);
				for (unsigned int k = 0; k < CInner::ElemMaxCount; k++)
					node.ChildIds[k] =  k;
				CNodeExtended<CInner> ext;
				Elem_t max;
				debugSetElem(max, i - 1);
				ext.node = &node;
				ext.insertionPoint = j;
				ext.maxElemCopy = &max;
				if (!simpleDelete(ext)) {
					result |= (1 << 14);
					btree_assert(!assertme);
				}

				unsigned char c = 0;
				NodeId_t kk = 0;
				for (unsigned int k = 0; k < i - 1; k++) {
					if (k == j) {
						c++;
						kk++;
					}
					if (debugGetElem(ext, k) != c++) {
						result |= (1 << 15);
						btree_assert(!assertme);
					}
					if (node.ChildIds[k] != kk++) {
						result |= (1 << 15);
						btree_assert(!assertme);
					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckMoveRightInner(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i <= CInner::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= CInner::ElemMaxCount; j++) {
				unsigned int maxMove = i < CInner::ElemMaxCount - j ? i : CInner::ElemMaxCount - j;
				for (unsigned int k = 1; k <= maxMove; k++) {
					CInner a, b;
					memset(a.Elems, 0xFF, sizeof(a.Elems));
					memset(b.Elems, 0xFF, sizeof(b.Elems));
					memset(a.ChildIds, 0xFF, sizeof(a.ChildIds));
					memset(b.ChildIds, 0xFF, sizeof(b.ChildIds));
					a.ElemCount = i;
					b.ElemCount = j;

					Elem_t ma = Elem_t(), mb = Elem_t();
					CNodeExtended<CInner> aExt, bExt;
					aExt.node = &a;
					aExt.maxElemCopy = &ma;
					bExt.node = &b;
					bExt.maxElemCopy = &mb;

					unsigned char c = 0;
					NodeId_t kk = 0;
					for (unsigned int u = 0; u < i; u++) {
						debugSetElem(aExt, u, c++);
						a.ChildIds[u] = kk++;
					}
					for (unsigned int u = 0; u < j; u++) {
						debugSetElem(bExt, u, c++);
						b.ChildIds[u] = kk++;
					}

					if (j) {
						const bool MoveToEmpty = false;
						if (k < i) {
							const bool MoveAll = false;
							moveRight<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						} else {
							const bool MoveAll = true;
							moveRight<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						}
					} else {
						const bool MoveToEmpty = true;
						if (k < i) {
							const bool MoveAll = false;
							moveRight<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						} else {
							const bool MoveAll = true;
							moveRight<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						}
					}

					if (a.ElemCount != (Pos_t)(i - k)) {
						result |= (1 << 16);
						btree_assert(!assertme);
					}
					if (b.ElemCount != (Pos_t)(j + k)) {
						result |= (1 << 16);
						btree_assert(!assertme);
					}

					c = 0;
					kk = 0;
					for (unsigned int u = 0; u < (unsigned int)a.ElemCount; u++) {
						if (debugGetElem(aExt, u) != c++) {
							result |= (1 << 17);
							btree_assert(!assertme);
						}
						if (a.ChildIds[u] != kk++) {
							result |= (1 << 17);
							btree_assert(!assertme);
						}
					}
					for (unsigned int u = 0; u < (unsigned int)b.ElemCount; u++) {
						if (debugGetElem(bExt, u) != c++) {
							result |= (1 << 17);
							btree_assert(!assertme);
						}
						if (b.ChildIds[u] != kk++) {
							result |= (1 << 17);
							btree_assert(!assertme);
						}
					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckMoveLeftInner(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i <= CInner::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= CInner::ElemMaxCount; j++) {
				unsigned int maxMove = j < CInner::ElemMaxCount - i ? j : CInner::ElemMaxCount - i;
				for (unsigned int k = 1; k <= maxMove; k++) {
					CInner a, b;
					memset(a.Elems, 0xFF, sizeof(a.Elems));
					memset(b.Elems, 0xFF, sizeof(b.Elems));
					memset(a.ChildIds, 0xFF, sizeof(a.ChildIds));
					memset(b.ChildIds, 0xFF, sizeof(b.ChildIds));
					a.ElemCount = i;
					b.ElemCount = j;

					Elem_t ma = Elem_t(), mb = Elem_t();
					CNodeExtended<CInner> aExt, bExt;
					aExt.node = &a;
					aExt.maxElemCopy = &ma;
					bExt.node = &b;
					bExt.maxElemCopy = &mb;

					unsigned char c = 0;
					NodeId_t kk = 0;
					for (unsigned int u = 0; u < i; u++) {
						debugSetElem(aExt, u, c++);
						a.ChildIds[u] = kk++;
					}
					for (unsigned int u = 0; u < j; u++) {
						debugSetElem(bExt, u, c++);
						b.ChildIds[u] = kk++;
					}

					if (i) {
						const bool MoveToEmpty = false;
						if (k < j) {
							const bool MoveAll = false;
							moveLeft<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						} else {
							const bool MoveAll = true;
							moveLeft<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						}
					} else {
						const bool MoveToEmpty = true;
						if (k < j) {
							const bool MoveAll = false;
							moveLeft<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						} else {
							const bool MoveAll = true;
							moveLeft<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)k);
						}
					}

					if (a.ElemCount != (Pos_t)(i + k)) {
						result |= (1 << 18);
						btree_assert(!assertme);
					}
					if (b.ElemCount != (Pos_t)(j - k)) {
						result |= (1 << 18);
						btree_assert(!assertme);
					}

					c = 0;
					kk = 0;
					for (unsigned int u = 0; u < (unsigned int)a.ElemCount; u++) {
						if (debugGetElem(aExt, u) != c++) {
							result |= (1 << 19);
							btree_assert(!assertme);
						}
						if (a.ChildIds[u] != kk++) {
							result |= (1 << 19);
							btree_assert(!assertme);
						}
					}
					for (unsigned int u = 0; u < (unsigned int)b.ElemCount; u++) {
						if (debugGetElem(bExt, u) != c++) {
							result |= (1 << 19);
							btree_assert(!assertme);
						}
						if (b.ChildIds[u] != kk++) {
							result |= (1 << 19);
							btree_assert(!assertme);
						}
					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckMoveRightInsertInner(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i <= CInner::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= CInner::ElemMaxCount; j++) {
				unsigned int maxMove = i + 1 < CInner::ElemMaxCount - j ? i + 1 : CInner::ElemMaxCount - j;
				for (unsigned int k = 0; k <= i; k++) {
					for (unsigned int u = 1; u <= maxMove; u++) {
						CInner a, b;
						memset(a.Elems, 0xFF, sizeof(a.Elems));
						memset(b.Elems, 0xFF, sizeof(b.Elems));
						memset(a.ChildIds, 0xFF, sizeof(a.ChildIds));
						memset(b.ChildIds, 0xFF, sizeof(b.ChildIds));
						a.ElemCount = i;
						b.ElemCount = j;

						Elem_t ma = Elem_t(), mb = Elem_t();
						CNodeExtended<CInner> aExt, bExt;
						aExt.node = &a;
						aExt.maxElemCopy = &ma;
						bExt.node = &b;
						bExt.maxElemCopy = &mb;

						unsigned char c = 0;
						NodeId_t kk = 0;
						unsigned char ic = i + j;
						NodeId_t ikk = (NodeId_t)(i + j);

						for (unsigned int v = 0; v < i; v++) {
							if (v == k) {
								ic = c++;
								ikk = kk++;
							}
							debugSetElem(aExt, v, c++);
							a.ChildIds[v] = kk++;
						}
						if (k == i) {
							ic = c++;
							ikk = kk++;
						}
						for (unsigned int v = 0; v < j; v++) {
							debugSetElem(bExt, v, c++);
							b.ChildIds[v] = kk++;
						}

						aExt.insertionPoint = -1;
						Elem_t ins;
						debugSetElem(ins, ic);
						CNodeExtended<CInner> insExt;
						insExt.posInParent = k;
						insExt.maxElemCopy = &ins;
						insExt.nodeId = ikk;

						if (j) {
							const bool MoveToEmpty = false;
							if (u < i + 1) {
								const bool MoveAll = false;
								moveRightInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, insExt);
							} else {
								const bool MoveAll = true;
								moveRightInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, insExt);
							}
						} else {
							const bool MoveToEmpty = true;
							if (u < i + 1) {
								const bool MoveAll = false;
								moveRightInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, insExt);
							} else {
								const bool MoveAll = true;
								moveRightInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, insExt);
							}
						}

						if (a.ElemCount != (Pos_t)(i - u + 1)) {
							result |= (1 << 20);
							btree_assert(!assertme);
						}
						if (b.ElemCount != (Pos_t)(j + u)) {
							result |= (1 << 20);
							btree_assert(!assertme);
						}

						c = 0;
						kk = 0;
						for (unsigned int v = 0; v < (unsigned int)a.ElemCount; v++) {
							if (debugGetElem(aExt, v) != c++) {
								result |= (1 << 21);
								btree_assert(!assertme);
							}
							if (a.ChildIds[v] != kk++) {
								result |= (1 << 21);
								btree_assert(!assertme);
							}
						}
						for (unsigned int v = 0; v < (unsigned int)b.ElemCount; v++) {
							if (debugGetElem(bExt, v) != c++) {
								result |= (1 << 21);
								btree_assert(!assertme);
							}
							if (b.ChildIds[v] != kk++) {
								result |= (1 << 21);
								btree_assert(!assertme);
							}
						}

					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheckMoveLeftInsertInner(bool assertme)
	{
		(void)assertme;
		int result = 0;
		for (unsigned int i = 0; i <= CInner::ElemMaxCount; i++) {
			for (unsigned int j = 0; j <= CInner::ElemMaxCount; j++) {
				unsigned int maxMove = j + 1 < CInner::ElemMaxCount - i ? j + 1 : CInner::ElemMaxCount - i;
				for (unsigned int k = 0; k <= j; k++) {
					for (unsigned int u = 1; u <= maxMove; u++) {
						CInner a, b;
						memset(a.Elems, 0xFF, sizeof(a.Elems));
						memset(b.Elems, 0xFF, sizeof(b.Elems));
						memset(a.ChildIds, 0xFF, sizeof(a.ChildIds));
						memset(b.ChildIds, 0xFF, sizeof(b.ChildIds));
						a.ElemCount = i;
						b.ElemCount = j;

						Elem_t ma = Elem_t(), mb = Elem_t();
						CNodeExtended<CInner> aExt, bExt;
						aExt.node = &a;
						aExt.maxElemCopy = &ma;
						bExt.node = &b;
						bExt.maxElemCopy = &mb;

						unsigned char c = 0;
						NodeId_t kk = 0;
						unsigned char ic = i + j;
						NodeId_t ikk = (NodeId_t)(i + j);
						for (unsigned int v = 0; v < i; v++) {
							debugSetElem(aExt, v, c++);
							a.ChildIds[v] = kk++;
						}
						for (unsigned int v = 0; v < j; v++) {
							if (v == k) {
								ic = c++;
								ikk = kk++;
							}
							debugSetElem(bExt, v, c++);
							b.ChildIds[v] = kk++;
						}

						bExt.insertionPoint = -1;
						Elem_t ins;
						debugSetElem(ins, ic);
						CNodeExtended<CInner> insExt;
						insExt.posInParent = k;
						insExt.maxElemCopy = &ins;
						insExt.nodeId = ikk;

						if (i) {
							const bool MoveToEmpty = false;
							if (u < j + 1) {
								const bool MoveAll = false;
								moveLeftInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, insExt);
							} else {
								const bool MoveAll = true;
								moveLeftInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, insExt);
							}
						} else {
							const bool MoveToEmpty = true;
							if (u < j + 1) {
								const bool MoveAll = false;
								moveLeftInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, insExt);
							} else {
								const bool MoveAll = true;
								moveLeftInsert<MoveToEmpty, MoveAll>(aExt, bExt, (Pos_t)u, insExt);
							}
						}

						if (a.ElemCount != (Pos_t)(i + u)) {
							result |= (1 << 22);
							btree_assert(!assertme);
						}
						if (b.ElemCount != (Pos_t)(j - u + 1)) {
							result |= (1 << 22);
							btree_assert(!assertme);
						}

						c = 0;
						kk = 0;
						for (unsigned int v = 0; v < (unsigned int)a.ElemCount; v++) {
							if (debugGetElem(aExt, v) != c++) {
								result |= (1 << 23);
								btree_assert(!assertme);
							}
							if (a.ChildIds[v] != kk++) {
								result |= (1 << 23);
								btree_assert(!assertme);
							}
						}
						for (unsigned int v = 0; v < (unsigned int)b.ElemCount; v++) {
							if (debugGetElem(bExt, v) != c++) {
								result |= (1 << 23);
								btree_assert(!assertme);
							}
							if (b.ChildIds[v] != kk++) {
								result |= (1 << 23);
								btree_assert(!assertme);
							}
						}

					}
				}
			}
		}
		return result;
	}

	int internalMechanismCheck(bool assertme)
	{
		int result = 0;
		result |= internalMechanismCheckInsertLeaf(assertme);
		result |= internalMechanismCheckDeleteLeaf(assertme);
		result |= internalMechanismCheckMoveRightLeaf(assertme);
		result |= internalMechanismCheckMoveLeftLeaf(assertme);
		result |= internalMechanismCheckMoveRightInsertLeaf(assertme);
		result |= internalMechanismCheckMoveLeftInsertLeaf(assertme);

		result |= internalMechanismCheckInsertInner(assertme);
		result |= internalMechanismCheckDeleteInner(assertme);
		result |= internalMechanismCheckMoveRightInner(assertme);
		result |= internalMechanismCheckMoveLeftInner(assertme);
		result |= internalMechanismCheckMoveRightInsertInner(assertme);
		result |= internalMechanismCheckMoveLeftInsertInner(assertme);
		return result;
	}

// Debug self-checking. Returns bitmask of found problems (0 on success)
public:
	int Check() const
	{
		int result = 0;
		if (!root) {
			if (depth != 0)
				result |= 0x1;
			if (count != 0)
				result |= 0x1;
			if (leafCount != 0 || internalCount != 0)
				result |= 0x1;
			return result;
		}
		if (maxElem != debugGetMaxElem(root))
			result |= 0x8;
		if (getNode(rootId) != root)
			result |= 0x2;
		size_t calcCount = 0;
		result |= checkNode(root, depth, calcCount);
		if (count != calcCount)
			result |= 0x4;
		return result;
	}

private:
	const Elem_t &debugGetMaxElem(const CLeaf *node) const
	{
		return node->LastElem();
	}
	const Elem_t &debugGetMaxElem(const CInner *node) const
	{
		const CNode *child = getNode(node->ChildIds[node->ElemCount - 1]);
		if (child->Type == CT_Leaf)
			return debugGetMaxElem(static_cast<const CLeaf *>(child));
		else
			return debugGetMaxElem(static_cast<const CInner *>(child));
	}
	const Elem_t &debugGetMaxElem(const CNode *node) const
	{
		if (node->Type == CT_Leaf)
			return debugGetMaxElem(static_cast<const CLeaf *>(node));
		else
			return debugGetMaxElem(static_cast<const CInner *>(node));
	}

	int checkNode(const CNode *unknode, int level, size_t &calcCount) const
	{
		if (unknode->Type != CT_Leaf && unknode->Type != CT_Inner)
			return 0x10;
		if (unknode->Type == CT_Leaf) {
			calcCount += unknode->ElemCount;
			const CLeaf *node = static_cast<const CLeaf *>(unknode);
			int result = 0;
			if (level != 1)
				result |= 0x100;
			if (node->ElemCount == 0)
				result |= 0x200;
			if (node->ElemCount > node->ElemMaxCount)
				result |= 0x200;
			for (Pos_t i = 1; i < node->ElemCount; i++)
				if (CComp::Comp(node->Elems[i - 1], node->Elems[i], param) >= 0)
					result |= 0x400;
			return result;
		} else {
			const CInner *node = static_cast<const CInner *>(unknode);
			int result = 0;
			if (node->ElemCount == 0)
				result |= 0x1000;
			if (node->ElemCount > node->ElemMaxCount)
				result |= 0x1000;
			for (Pos_t i = 1; i < node->ElemCount - 1; i++)
				if (CComp::Comp(node->Elems[i - 1], node->Elems[i], param) >= 0)
					result |= 0x2000;
			for (Pos_t i = 0; i < node->ElemCount - 1; i++)
				if (node->Elems[i] != debugGetMaxElem(getNode(node->ChildIds[i])))
					result |= 0x4000;
			if (node->ElemCount > 1)
				if (CComp::Comp(node->Elems[node->ElemCount - 2], debugGetMaxElem(node), param) >= 0)
					result |= 0x8000;
			for (Pos_t i = 0; i < node->ElemCount; i++)
				result |= checkNode(getNode(node->ChildIds[i]), level - 1, calcCount);
			return result;
		}
	}

// Debug printing to output stream
public:
	template <class CStream>
	void Print(CStream &stream) const
	{
		if (!root) {
			stream << "Empty\n";
			return;
		}
		print(stream, root);
	}

private:
	template <class CStream>
	static void printIndent(CStream &stream, int indent)
	{
		for (int i = 0; i < indent; i++)
			stream << "  ";
	}

	template <class CStream>
	void print(CStream &stream, const CLeaf* node, int indent) const
	{
		printIndent(stream, indent);
		stream << "[(" << node->ElemCount << ")";
		for(Pos_t i = 0; i < node->ElemCount; i++)
			stream << " " << node->Elems[i];
		stream << "]\n";
	}

	template <class CStream>
	void print(CStream &stream, const CInner* node, int indent) const
	{
		CNode *next = getNode(node->ChildIds[0]);
		print(stream, next, indent+1);
		for(Pos_t i = 0; i < node->ElemCount - 1; i++) {
			printIndent(stream, indent);
			stream << node->Elems[i] << "\n";
			next = getNode(node->ChildIds[i + 1]);
			print(stream, next, indent+1);
		}
	}

	template <class CStream>
	void print(CStream &stream, const CNode* node, int indent = 0) const
	{
		if(node->Type == CT_Inner)
			print(stream, static_cast<const CInner *>(node), indent);
		else
			print(stream, static_cast<const CLeaf *>(node), indent);
	}


};
