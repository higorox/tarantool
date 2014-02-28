#pragma once
#include "Providence.h"
#include <assert.h>
#include <string.h>

#ifdef __OPTIMIZE__
#define btree_assert(e) do {} while(0)
#else
#define btree_assert(e) assert(e)
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

#define CT_ASSERT_TYPEIS(T, U) do { typedef char __ct_assert[(CTTypeIs<T, U>::res) ? 1 : -1]; } while(0)
#define CT_ASSERT_TYPEISOR(T, U, V) do { typedef char __ct_assert[(CTTypeIs<T, U>::res || CTTypeIs<T, V>::res) ? 1 : -1]; } while(0)
// Compile time utils END


#ifdef _DEBUG
#define MEMMOVE(dst, src, num, dstNode, srcNode) debugMemMove(dst, src, num, dstNode, srcNode)
#else
#define MEMMOVE(dst, src, num, dstNode, srcNode) memmove(dst, src, num)
#endif

template<class Elem_t, size_t NodeSize, class CAllocator, size_t AllocSize, int AllocLevel, class CComp, class CompPar_t>
class CCensoredTree {
public:
	enum ChunkType {
		CT_Garbage,
		CT_Inner,
		CT_Leaf
	};

	typedef unsigned int NodeId_t;
	typedef short int Pos_t;

	struct CPointer {
		NodeId_t NodeId;
		Pos_t Pos;
		bool IsNull() { return NodeId == NodeId_t(-1); }
		void SetNull() { NodeId = NodeId_t(-1); }
		void Set(NodeId_t _nodeId, Pos_t _pos) { NodeId = _nodeId; Pos = _pos; }
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
		CLeaf() : CNode(CT_Leaf) {}
		void Set(const Elem_t &newElem) { CNode::ElemCount = 1; Elems[0] = newElem; }
		void Set() { CNode::ElemCount = 0; }
		enum { ElemsMaxCount = (NodeSize - sizeof(CNode)) / sizeof(Elem_t) };
		Elem_t Elems[ElemsMaxCount];
		Elem_t &LastElem() { return Elems[CNode::ElemCount-1]; }
		Pos_t SpareCount() { return ElemsMaxCount - CNode::ElemCount; }
		Pos_t OverCount() { return CNode::ElemCount - ElemsMaxCount * 2 / 3; }
	};
	struct CInner : CNode {
		CInner() : CNode(CT_Inner) {}
		void Set(NodeId_t a, NodeId_t b, const Elem_t &newElem) { CNode::ElemCount = 1; ChildIds[0] = a; ChildIds[1] = b; Elems[0] = newElem; }
		enum { ElemsMaxCount = (NodeSize - sizeof(CNode) - sizeof(NodeId_t)) / (sizeof(Elem_t) + sizeof(NodeId_t)) };
		NodeId_t ChildIds[ElemsMaxCount + 1];
		Elem_t Elems[ElemsMaxCount];
		Elem_t &LastElem() { return Elems[CNode::ElemCount-1]; }
		Pos_t SpareCount() { return ElemsMaxCount - CNode::ElemCount; }
		Pos_t OverCount() { return CNode::ElemCount - ElemsMaxCount * 2 / 3; }
	};
	template<class Node_t>
	struct CNodeInfo {
		Node_t *node;
		NodeId_t nodeId;
		CNodeInfo<CInner> *parentInfo;
		Pos_t insertionPoint;
		Pos_t posInParent;
		Elem_t *maxElemCopy;
	};

	void debugMemMove(void *dst, const void *src, size_t num, CNode *dstNode, CNode *srcNode)
	{
		(void)dstNode;
		(void)srcNode;
		btree_assert(dstNode->Type == CT_Leaf || dstNode->Type == CT_Inner);
		btree_assert(srcNode->Type == CT_Leaf || srcNode->Type == CT_Inner);
		btree_assert(dstNode->Type == srcNode->Type);

		char *dst_c = static_cast<char *>(dst);
		const char *src_c = static_cast<const char *>(src);

		if (dstNode->Type == CT_Leaf) {
			CLeaf *dstLeaf = static_cast<CLeaf *>(dstNode);
			CLeaf *srcLeaf = static_cast<CLeaf *>(srcNode);
			char *dstBufBeg = reinterpret_cast<char *>(dstLeaf->Elems);
			char *dstBufEnd = reinterpret_cast<char *>(dstLeaf->Elems + dstLeaf->ElemsMaxCount);
			char *srcBufBeg = reinterpret_cast<char *>(srcLeaf->Elems);
			char *srcBufEnd = reinterpret_cast<char *>(srcLeaf->Elems + srcLeaf->ElemsMaxCount);
			btree_assert(dst_c >= dstBufBeg && dst_c <= dstBufEnd);
			btree_assert(dst_c + num >= dstBufBeg && dst_c + num <= dstBufEnd);
			btree_assert(src_c >= srcBufBeg && src_c <= srcBufEnd);
			btree_assert(src_c + num >= srcBufBeg && src_c + num <= srcBufEnd);
		} else {
			CInner *dstInner = static_cast<CInner *>(dstNode);
			CInner *srcInner = static_cast<CInner *>(srcNode);
			char *dstBuf1Beg = reinterpret_cast<char *>(dstInner->Elems);
			char *dstBuf1End = reinterpret_cast<char *>(dstInner->Elems + dstInner->ElemsMaxCount);
			char *srcBuf1Beg = reinterpret_cast<char *>(srcInner->Elems);
			char *srcBuf1End = reinterpret_cast<char *>(srcInner->Elems + srcInner->ElemsMaxCount);
			char *dstBuf2Beg = reinterpret_cast<char *>(dstInner->ChildIds);
			char *dstBuf2End = reinterpret_cast<char *>(dstInner->ChildIds + dstInner->ElemsMaxCount + 1);
			char *srcBuf2Beg = reinterpret_cast<char *>(srcInner->ChildIds);
			char *srcBuf2End = reinterpret_cast<char *>(srcInner->ChildIds + srcInner->ElemsMaxCount + 1);
			btree_assert((dst_c >= dstBuf1Beg && dst_c <= dstBuf1End) || (dst_c >= dstBuf2Beg && dst_c <= dstBuf2End));
			if (dst_c >= dstBuf1Beg && dst_c < dstBuf1End) {
				btree_assert(dst_c + num >= dstBuf1Beg && dst_c + num <= dstBuf1End);
				btree_assert(src_c >= srcBuf1Beg && src_c <= srcBuf1End);
				btree_assert(src_c + num >= srcBuf1Beg && src_c + num <= srcBuf1End);
			} else {
				btree_assert(dst_c + num >= dstBuf2Beg && dst_c + num <= dstBuf2End);
				btree_assert(src_c >= srcBuf2Beg && src_c <= srcBuf2End);
				btree_assert(src_c + num >= srcBuf2Beg && src_c + num <= srcBuf2End);
			}
		}
		memmove(dst, src, num);
	}

	bool insertFirstElem(const Elem_t& newElem, bool allowInsert)
	{
		btree_assert(depth == 0);
		btree_assert(count == 0);
		btree_assert(leafCount == 0);
		if (!allowInsert)
			return false;
		CLeaf *leaf = newNode<CLeaf>(rootId);
		leaf->Set(newElem);
		root = leaf;
		depth = 1;
		count = 1;
		return true;
	}

	Elem_t &getMaxElem(CLeaf *node) const
	{
		return node->LastElem();
	}
	Elem_t &getMaxElem(CInner *node) const
	{
		CNode *child = getNode(node->ChildIds[node->ElemCount]);
		if (child->Type == CT_Leaf)
			return getMaxElem(static_cast<CLeaf *>(child));
		else
			return getMaxElem(static_cast<CInner *>(child));
	}
	Elem_t &getMaxElem(CNode *node) const
	{
		if (node->Type == CT_Leaf)
			return getMaxElem(static_cast<CLeaf *>(node));
		else
			return getMaxElem(static_cast<CInner *>(node));
	}

	const Elem_t &getMaxElem(const CLeaf *node) const
	{
		return node->LastElem();
	}
	const Elem_t &getMaxElem(const CInner *node) const
	{
		CNode *child = getNode(node->ChildIds[node->ElemCount]);
		if (child->Type == CT_Leaf)
			return getMaxElem(static_cast<CLeaf *>(child));
		else
			return getMaxElem(static_cast<CInner *>(child));
	}
	const Elem_t &getMaxElem(const CNode *node) const
	{
		if (node->Type == CT_Leaf)
			return getMaxElem(static_cast<CLeaf *>(node));
		else
			return getMaxElem(static_cast<CInner *>(node));
	}

	bool processReplace(CNodeInfo<CLeaf> &lastInfo, const Elem_t &newElem, Elem_t *replaced)
	{
		if (replaced)
			*replaced = lastInfo.node->Elems[lastInfo.insertionPoint];
		if (lastInfo.insertionPoint == lastInfo.node->ElemCount - 1) {
			if (lastInfo.maxElemCopy) {
				btree_assert(lastInfo.node->Elems[lastInfo.insertionPoint] == *lastInfo.maxElemCopy);
				*lastInfo.maxElemCopy = newElem;
			}
		}
		lastInfo.node->Elems[lastInfo.insertionPoint] = newElem;
		return true;
	}

	bool simpleInsert(CNodeInfo<CLeaf> &info, const Elem_t &newElem)
	{
		if (info.insertionPoint < info.node->ElemCount) {
			Elem_t *block = info.node->Elems + info.insertionPoint;
			Pos_t blockSize = info.node->ElemCount - info.insertionPoint;
			MEMMOVE(block + 1, block, blockSize * sizeof(Elem_t), info.node, info.node);
		} else {
			if (info.maxElemCopy)
				*info.maxElemCopy = newElem;
		}
		info.node->Elems[info.insertionPoint] = newElem;
		info.node->ElemCount++;
		count++;
		return true;
	}
	bool simpleDelete(CNodeInfo<CLeaf> &info)
	{
		btree_assert(info.insertionPoint < info.node->ElemCount);
		if (info.insertionPoint < info.node->ElemCount - 1) {
			Elem_t *block = info.node->Elems + info.insertionPoint;
			Pos_t blockSize = info.node->ElemCount - info.insertionPoint - 1;
			MEMMOVE(block, block + 1, blockSize * sizeof(Elem_t), info.node, info.node);
		} else {
			if (info.maxElemCopy && info.node->ElemCount > 1)
				*info.maxElemCopy = info.node->Elems[info.node->ElemCount - 2];
		}
		info.node->ElemCount--;
		count--;
		return true;
	}
	bool moveRight(CNodeInfo<CLeaf> &aInfo, CNodeInfo<CLeaf> &bInfo, Pos_t num)
	{
		CLeaf *a = aInfo.node;
		CLeaf *b = bInfo.node;
		MEMMOVE(b->Elems + num, b->Elems, b->ElemCount * sizeof(Elem_t), b, b);
		MEMMOVE(b->Elems, a->Elems + a->ElemCount - num, num * sizeof(Elem_t), b, a);
		a->ElemCount -= num;
		b->ElemCount += num;
		if (aInfo.maxElemCopy)
			*aInfo.maxElemCopy = aInfo.node->LastElem();
		return true;
	}
	bool moveLeft(CNodeInfo<CLeaf> &aInfo, CNodeInfo<CLeaf> &bInfo, Pos_t num)
	{
		CLeaf *a = aInfo.node;
		CLeaf *b = bInfo.node;
		MEMMOVE(a->Elems + a->ElemCount, b->Elems, num * sizeof(Elem_t), a, b);
		MEMMOVE(b->Elems, b->Elems + num, (b->ElemCount - num) * sizeof(Elem_t), b, b);
		a->ElemCount += num;
		b->ElemCount -= num;
		if (aInfo.maxElemCopy)
			*aInfo.maxElemCopy = aInfo.node->LastElem();
		return true;
	}
	bool moveRightInsert(CNodeInfo<CLeaf> &aInfo, CNodeInfo<CLeaf> &bInfo, Pos_t num, const Elem_t &newElem)
	{
		Pos_t move1 = aInfo.node->ElemCount - aInfo.insertionPoint;
		if (move1 >= num) {
			moveRight(aInfo, bInfo, num);
			simpleInsert(aInfo, newElem);
		} else {
			Pos_t move2 = num - move1 - 1;
			CLeaf *a = aInfo.node;
			CLeaf *b = bInfo.node;
			MEMMOVE(b->Elems + num, b->Elems, b->ElemCount * sizeof(Elem_t), b, b);
			MEMMOVE(b->Elems + num - move1, a->Elems + a->ElemCount - move1, move1 * sizeof(Elem_t), b, a);
			b->Elems[num - move1 - 1] = newElem;
			MEMMOVE(b->Elems, a->Elems + a->ElemCount - num + 1, move2 * sizeof(Elem_t), b, a);
			a->ElemCount -= (num - 1);
			b->ElemCount += num;
			if (aInfo.maxElemCopy)
				*aInfo.maxElemCopy = aInfo.node->LastElem();
			count++;
		}
		return true;
	}
	bool moveLeftInsert(CNodeInfo<CLeaf> &aInfo, CNodeInfo<CLeaf> &bInfo, Pos_t num, const Elem_t &newElem)
	{
		Pos_t move1 = bInfo.insertionPoint;
		if (move1 >= num) {
			// One can improve
			moveLeft(aInfo, bInfo, num);
			bInfo.insertionPoint -= num;
			simpleInsert(bInfo, newElem);
		} else {
			Pos_t move2 = num - move1 - 1;
			CLeaf *a = aInfo.node;
			CLeaf *b = bInfo.node;
			MEMMOVE(a->Elems + a->ElemCount, b->Elems, move1 * sizeof(Elem_t), a, b);
			a->Elems[a->ElemCount + move1] = newElem;
			MEMMOVE(a->Elems + a->ElemCount + move1 + 1, b->Elems + move1, move2 * sizeof(Elem_t), a, b);
			MEMMOVE(b->Elems, b->Elems + num - 1, (b->ElemCount - num + 1) * sizeof(Elem_t), b, b);
			a->ElemCount += num;
			b->ElemCount -= (num - 1);
			if (aInfo.maxElemCopy)
				*aInfo.maxElemCopy = aInfo.node->LastElem();
			count++;
		}
		return true;
	}

	bool simpleInsert(CNodeInfo<CInner> &info, const Elem_t &newElem, NodeId_t newNodeId)
	{
		if (info.node->ElemCount == Pos_t(-1)) {
			btree_assert(info.maxElemCopy == 0);
			info.node->ElemCount = 0;
			info.node->ChildIds[0] = newNodeId;
			return true;
		}
		Pos_t insertionPoint = info.insertionPoint + 1;
		if (insertionPoint < info.node->ElemCount) {
			Elem_t *block1 = info.node->Elems + insertionPoint;
			Pos_t blockSize1 = info.node->ElemCount - insertionPoint;
			MEMMOVE(block1 + 1, block1, blockSize1 * sizeof(*block1), info.node, info.node);
			NodeId_t *block2 = info.node->ChildIds + insertionPoint;
			Pos_t blockSize2 = blockSize1 + 1;
			MEMMOVE(block2 + 1, block2, blockSize2 * sizeof(*block2), info.node, info.node);
			info.node->Elems[insertionPoint] = newElem;
			info.node->ChildIds[insertionPoint] = newNodeId;
		} else if (insertionPoint == info.node->ElemCount) {
			info.node->Elems[insertionPoint] = newElem;
			info.node->ChildIds[insertionPoint + 1] = info.node->ChildIds[insertionPoint];
			info.node->ChildIds[insertionPoint] = newNodeId;
		} else {
			btree_assert(insertionPoint == info.node->ElemCount + 1);

			info.node->ChildIds[insertionPoint] = newNodeId;
			if (info.maxElemCopy) {
				info.node->Elems[info.node->ElemCount] = *info.maxElemCopy;
				*info.maxElemCopy = newElem;
			} else {
				info.node->Elems[info.node->ElemCount] = getMaxElem(info.node);
			}
		}
		info.node->ElemCount++;
		return true;
	}

	bool simpleDelete(CNodeInfo<CInner> &info)
	{
		btree_assert(info.insertionPoint <= info.node->ElemCount);
		if (info.insertionPoint < info.node->ElemCount - 1) {
			Elem_t *block1 = info.node->Elems + info.insertionPoint;
			Pos_t blockSize1 = info.node->ElemCount - info.insertionPoint - 1;
			MEMMOVE(block1, block1 + 1, blockSize1 * sizeof(*block1), info.node, info.node);
			NodeId_t *block2 = info.node->ChildIds + info.insertionPoint;
			Pos_t blockSize2 = blockSize1 + 1;
			MEMMOVE(block2, block2 + 1, blockSize2 * sizeof(*block2), info.node, info.node);
		} else if (info.insertionPoint == info.node->ElemCount - 1) {
			info.node->ChildIds[info.node->ElemCount - 1] = info.node->ChildIds[info.node->ElemCount];
		} else {
			if (info.maxElemCopy && info.node->ElemCount > 0)
				*info.maxElemCopy = getMaxElem(getNode(info.node->ChildIds[info.node->ElemCount - 1]));
				//*info.maxElemCopy = info.node->Elems[info.node->ElemCount - 1];
		}
		info.node->ElemCount--;
		return true;
	}

	bool moveRight(CNodeInfo<CInner> &aInfo, CNodeInfo<CInner> &bInfo, Pos_t num)
	{
		CInner *a = aInfo.node;
		CInner *b = bInfo.node;
		if (b->ElemCount == Pos_t(-1)) {
			btree_assert(bInfo.maxElemCopy == 0);
			b->ChildIds[0] = a->ChildIds[a->ElemCount];
			a->ElemCount--;
			if (aInfo.maxElemCopy)
				*aInfo.maxElemCopy = a->Elems[a->ElemCount];
			num--;
			b->ElemCount = 0;
			if (!num)
				return true;
		}
		MEMMOVE(b->Elems + num, b->Elems, b->ElemCount * sizeof(Elem_t), b, b);
		MEMMOVE(b->Elems, a->Elems + a->ElemCount - num + 1, (num - 1) * sizeof(Elem_t), b, a);
		MEMMOVE(b->ChildIds + num, b->ChildIds, (b->ElemCount + 1) * sizeof(NodeId_t), b, b);
		MEMMOVE(b->ChildIds, a->ChildIds + a->ElemCount + 1 - num, num * sizeof(NodeId_t), b, a);
		a->ElemCount -= num;
		b->ElemCount += num;
		if (aInfo.maxElemCopy) {
			b->Elems[num - 1] = *aInfo.maxElemCopy;
			if (a->ElemCount >= 0)
				*aInfo.maxElemCopy = a->Elems[a->ElemCount];
		} else {
			// I guess it's case of splitting the most right node on level
			b->Elems[num - 1] = getMaxElem(getNode(b->ChildIds[num - 1]));
		}
		return true;
	}
	bool moveLeft(CNodeInfo<CInner> &aInfo, CNodeInfo<CInner> &bInfo, Pos_t num)
	{
		CInner *a = aInfo.node;
		CInner *b = bInfo.node;
		if (a->ElemCount == Pos_t(-1)) {
			btree_assert(aInfo.maxElemCopy == 0);
			a->ChildIds[0] = b->ChildIds[0];
			a->ElemCount = 0;
			MEMMOVE(b->Elems, b->Elems + 1, (b->ElemCount - 1) * sizeof(Elem_t), b, b);
			MEMMOVE(b->ChildIds, b->ChildIds + 1, (b->ElemCount) * sizeof(NodeId_t), b, b);
			b->ElemCount--;
			num--;
			if (!num)
				return true;
		}
		if (aInfo.maxElemCopy) {
			a->Elems[a->ElemCount] = *aInfo.maxElemCopy;
			*aInfo.maxElemCopy = b->Elems[num - 1];
		} else {
			// Moving to new node
			a->Elems[a->ElemCount] = getMaxElem(getNode(aInfo.node->ChildIds[a->ElemCount]));
		}
		if (num > 1)
			MEMMOVE(a->Elems + a->ElemCount + 1, b->Elems, (num - 1) * sizeof(Elem_t), a, b);
		if (b->ElemCount > num)
			MEMMOVE(b->Elems, b->Elems + num, (b->ElemCount - num) * sizeof(Elem_t), b, b);
		MEMMOVE(a->ChildIds + a->ElemCount + 1, b->ChildIds, num * sizeof(NodeId_t), a ,b);
		MEMMOVE(b->ChildIds, b->ChildIds + num, (b->ElemCount + 1 - num) * sizeof(NodeId_t), b, b);
		a->ElemCount += num;
		b->ElemCount -= num;
		return true;
	}
	bool moveRightInsert(CNodeInfo<CInner> &aInfo, CNodeInfo<CInner> &bInfo, Pos_t num, const Elem_t &maxElem, NodeId_t newNode)
	{
		Pos_t insertionPoint = aInfo.insertionPoint + 1;
		Pos_t move1 = aInfo.node->ElemCount - insertionPoint + 1;
		if (move1 >= num) {
			moveRight(aInfo, bInfo, num);
			simpleInsert(aInfo, maxElem, newNode);
		} else {
			Pos_t move2 = num - move1 - 1;
			//? improve point
			CInner *b = bInfo.node;
			if (move1 != Pos_t(-1) && move1 != 0)
				moveRight(aInfo, bInfo, move1);
			if (b->ElemCount != Pos_t(-1))
				MEMMOVE(b->Elems + 1, b->Elems, b->ElemCount * sizeof(Elem_t), b, b);
			b->Elems[0] = maxElem;
			MEMMOVE(b->ChildIds + 1, b->ChildIds, (b->ElemCount + 1) * sizeof(NodeId_t), b, b);
			b->ChildIds[0] = newNode;
			b->ElemCount++;
			if (move2)
				moveRight(aInfo, bInfo, move2);
		}
		return true;
	}
	bool moveLeftInsert(CNodeInfo<CInner> &aInfo, CNodeInfo<CInner> &bInfo, Pos_t num, const Elem_t &maxElem, NodeId_t newNode)
	{
		Pos_t insertionPoint = bInfo.insertionPoint + 1;
		Pos_t move1 = insertionPoint;
		if (move1 >= num) {
			//? one can improve
			moveLeft(aInfo, bInfo, num);
			bInfo.insertionPoint -= num;
			simpleInsert(bInfo, maxElem, newNode);
		} else {
			Pos_t move2 = num - move1 - 1;
			//? improve point
			btree_assert(move1);
			moveLeft(aInfo, bInfo, move1);
			bInfo.insertionPoint -= move1;
			simpleInsert(bInfo, maxElem, newNode);
			if (move2)
				moveLeft(aInfo, bInfo, move2);
		}
		return true;
	}

	bool processInsert(CNodeInfo<CInner> &lastInfo, const Elem_t &maxElem, NodeId_t newNodeId)
	{
		if (lastInfo.node->SpareCount())
			return simpleInsert(lastInfo, maxElem, newNodeId);
		CNodeInfo<CInner> leftSib, rightSib, leftLeftSib, rightRightSib;
		bool hasLeftSib = collectLeftSib(lastInfo, leftSib);
		bool hasRightSib = collectRightSib(lastInfo, rightSib);
		bool hasLeftLeftSib = false;
		bool hasRightRightSib = false;
		if (hasLeftSib && hasRightSib) {
			if (leftSib.node->SpareCount() > rightSib.node->SpareCount()) {
				Pos_t moveCount = 1 + leftSib.node->SpareCount() / 2;
				return moveLeftInsert(leftSib, lastInfo, moveCount, maxElem, newNodeId);
			} else if (rightSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + rightSib.node->SpareCount() / 2;
				return moveRightInsert(lastInfo, rightSib, moveCount, maxElem, newNodeId);
			}
		} else if (hasLeftSib) {
			if (leftSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + leftSib.node->SpareCount() / 2;
				return moveLeftInsert(leftSib, lastInfo, moveCount, maxElem, newNodeId);
			}
			hasLeftLeftSib = collectLeftSib(leftSib, leftLeftSib);
			if (hasLeftLeftSib && leftLeftSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + (2 * leftLeftSib.node->SpareCount() - 1) / 3;
				moveLeft(leftLeftSib, leftSib, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveLeftInsert(leftSib, lastInfo, moveCount, maxElem, newNodeId);
			}
		} else if (hasRightSib) {
			if (rightSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + rightSib.node->SpareCount() / 2;
				return moveRightInsert(lastInfo, rightSib, moveCount, maxElem, newNodeId);
			}
			hasRightRightSib = collectRightSib(rightSib, rightRightSib);
			if (hasRightRightSib && rightRightSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + (2 * rightRightSib.node->SpareCount() - 1) / 3;
				moveRight(rightSib, rightRightSib, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveRightInsert(lastInfo, rightSib, moveCount, maxElem, newNodeId);
			}
		}
		NodeId_t anotherNewNodeId;
		CInner *newInner = newNode<CInner>(anotherNewNodeId);
		newInner->ElemCount = Pos_t(-1);
		CNodeInfo<CInner> newNodeInfo;
		prepareNewSib(lastInfo, newNodeInfo, newInner, anotherNewNodeId);
		if (hasLeftSib && hasRightSib) {
			Pos_t moveCount = newInner->ElemsMaxCount / 4;
			moveRightInsert(lastInfo, newNodeInfo, moveCount * 2, maxElem, newNodeId);
			moveLeft(newNodeInfo, rightSib, moveCount);
			moveRight(leftSib, lastInfo, moveCount);
		} else if (hasLeftSib && hasLeftLeftSib) {
			Pos_t moveCount = newInner->ElemsMaxCount / 4;
			moveRightInsert(lastInfo, newNodeInfo, moveCount * 3, maxElem, newNodeId);
			moveRight(leftSib, lastInfo, moveCount * 2);
			moveRight(leftLeftSib, leftSib, moveCount);
		} else if (hasRightSib && hasRightRightSib) {
			Pos_t moveCount = newInner->ElemsMaxCount / 4;
			moveRightInsert(lastInfo, newNodeInfo, moveCount, maxElem, newNodeId);
			moveLeft(newNodeInfo, rightSib, moveCount * 2);
			moveLeft(rightSib, rightRightSib, moveCount);
		} else if (hasLeftSib) {
			Pos_t moveCount = newInner->ElemsMaxCount / 3;
			moveRightInsert(lastInfo, newNodeInfo, moveCount * 2, maxElem, newNodeId);
			moveRight(leftSib, lastInfo, moveCount);
		} else if (hasRightSib) {
			Pos_t moveCount = newInner->ElemsMaxCount / 3;
			moveRightInsert(lastInfo, newNodeInfo, moveCount, maxElem, newNodeId);
			moveLeft(newNodeInfo, rightSib, moveCount);
		} else {
			btree_assert(lastInfo.parentInfo == 0);
			Pos_t moveCount = newInner->ElemsMaxCount / 2;
			moveRightInsert(lastInfo, newNodeInfo, moveCount, maxElem, newNodeId);

			NodeId_t newRootId;
			CInner *newRoot = newNode<CInner>(newRootId);
			newRoot->Set(rootId, anotherNewNodeId, getMaxElem(lastInfo.node));
			root = newRoot;
			rootId = newRootId;
			depth++;
			return true;
		}
		btree_assert(lastInfo.parentInfo);
		return processInsert(*lastInfo.parentInfo, getMaxElem(newInner), anotherNewNodeId);
	}

	bool processInsert(CNodeInfo<CLeaf> &lastInfo, const Elem_t &newElem)
	{
		if (lastInfo.node->SpareCount())
			return simpleInsert(lastInfo, newElem);
		CNodeInfo<CLeaf> leftSib, rightSib, leftLeftSib, rightRightSib;
		bool hasLeftSib = collectLeftSib(lastInfo, leftSib);
		bool hasRightSib = collectRightSib(lastInfo, rightSib);
		bool hasLeftLeftSib = false;
		bool hasRightRightSib = false;
		if (hasLeftSib && hasRightSib) {
			if (leftSib.node->SpareCount() > rightSib.node->SpareCount()) {
				Pos_t moveCount = 1 + leftSib.node->SpareCount() / 2;
				return moveLeftInsert(leftSib, lastInfo, moveCount, newElem);
			} else if (rightSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + rightSib.node->SpareCount() / 2;
				return moveRightInsert(lastInfo, rightSib, moveCount, newElem);
			}
		} else if (hasLeftSib) {
			if (leftSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + leftSib.node->SpareCount() / 2;
				return moveLeftInsert(leftSib, lastInfo, moveCount, newElem);
			}
			hasLeftLeftSib = collectLeftSib(leftSib, leftLeftSib);
			if (hasLeftLeftSib && leftLeftSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + (2 * leftLeftSib.node->SpareCount() - 1) / 3;
				moveLeft(leftLeftSib, leftSib, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveLeftInsert(leftSib, lastInfo, moveCount, newElem);
			}
		} else if (hasRightSib) {
			if (rightSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + rightSib.node->SpareCount() / 2;
				return moveRightInsert(lastInfo, rightSib, moveCount, newElem);
			}
			hasRightRightSib = collectRightSib(rightSib, rightRightSib);
			if (hasRightRightSib && rightRightSib.node->SpareCount() > 0) {
				Pos_t moveCount = 1 + (2 * rightRightSib.node->SpareCount() - 1) / 3;
				moveRight(rightSib, rightRightSib, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveRightInsert(lastInfo, rightSib, moveCount, newElem);
			}
		}
		NodeId_t newNodeId;
		CLeaf *newLeaf = newNode<CLeaf>(newNodeId);
		newLeaf->Set();
		CNodeInfo<CLeaf> newNodeInfo;
		prepareNewSib(lastInfo, newNodeInfo, newLeaf, newNodeId);
		if (hasLeftSib && hasRightSib) {
			Pos_t moveCount = newLeaf->ElemsMaxCount / 4;
			moveRightInsert(lastInfo, newNodeInfo, moveCount * 2, newElem);
			moveLeft(newNodeInfo, rightSib, moveCount);
			moveRight(leftSib, lastInfo, moveCount);
		} else if (hasLeftSib && hasLeftLeftSib) {
			Pos_t moveCount = newLeaf->ElemsMaxCount / 4;
			moveRightInsert(lastInfo, newNodeInfo, moveCount * 3, newElem);
			moveRight(leftSib, lastInfo, moveCount * 2);
			moveRight(leftLeftSib, leftSib, moveCount);
		} else if (hasRightSib && hasRightRightSib) {
			Pos_t moveCount = newLeaf->ElemsMaxCount / 4;
			moveRightInsert(lastInfo, newNodeInfo, moveCount, newElem);
			moveLeft(newNodeInfo, rightSib, moveCount * 2);
			moveLeft(rightSib, rightRightSib, moveCount);
		} else if (hasLeftSib) {
			Pos_t moveCount = newLeaf->ElemsMaxCount / 3;
			moveRightInsert(lastInfo, newNodeInfo, moveCount * 2, newElem);
			moveRight(leftSib, lastInfo, moveCount);
		} else if (hasRightSib) {
			Pos_t moveCount = newLeaf->ElemsMaxCount / 3;
			moveRightInsert(lastInfo, newNodeInfo, moveCount, newElem);
			moveLeft(newNodeInfo, rightSib, moveCount);
		} else {
			btree_assert(lastInfo.parentInfo == 0);
			Pos_t moveCount = newLeaf->ElemsMaxCount / 2;
			moveRightInsert(lastInfo, newNodeInfo, moveCount, newElem);

			NodeId_t newRootId;
			CInner *newRoot = newNode<CInner>(newRootId);
			newRoot->Set(rootId, newNodeId, lastInfo.node->LastElem());
			root = newRoot;
			rootId = newRootId;
			depth++;
			return true;
		}
		btree_assert(lastInfo.parentInfo);
		return processInsert(*lastInfo.parentInfo, newLeaf->LastElem(), newNodeId);
	}

	bool Insert(const Elem_t &newElem, bool allowInsert = true, bool allowReplace = true, Elem_t *replaced = 0)
	{
		btree_assert(allowInsert || allowReplace);
		if (!root)
			return insertFirstElem(newElem, allowInsert);
#ifdef WIN32
		CNodeInfo<CInner> pathInfo[20];
		btree_assert(depth < sizeof(pathInfo) / sizeof(pathInfo[0]));
#else
		CNodeInfo<CInner> pathInfo[depth - 1];
#endif
		CNodeInfo<CLeaf> lastInfo;
		bool exact;
		collectPath(newElem, pathInfo, lastInfo, exact);

		if ((exact && !allowReplace) || (!exact && !allowInsert))
			return false;

		if (exact)
			return processReplace(lastInfo, newElem, replaced);
		else
			return processInsert(lastInfo, newElem);
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

	template<class Node_t>
	bool processDelete(CNodeInfo<Node_t> &lastInfo)
	{
		simpleDelete(lastInfo);

		if (lastInfo.node->ElemCount >= lastInfo.node->ElemsMaxCount * 2 / 3)
			return true;

		CNodeInfo<Node_t> leftSib, rightSib, leftLeftSib, rightRightSib;
		bool hasLeftSib = collectLeftSib(lastInfo, leftSib);
		bool hasRightSib = collectRightSib(lastInfo, rightSib);
		bool hasLeftLeftSib = false;
		bool hasRightRightSib = false;
		if (hasLeftSib && hasRightSib) {
			if (leftSib.node->OverCount() > rightSib.node->OverCount()) {
				Pos_t moveCount = 1 + leftSib.node->OverCount() / 2;
				return moveRight(leftSib, lastInfo, moveCount);
			} else if (rightSib.node->OverCount() > 0) {
				Pos_t moveCount = 1 + rightSib.node->OverCount() / 2;
				return moveLeft(lastInfo, rightSib, moveCount);
			}
		} else if (hasLeftSib) {
			if (leftSib.node->OverCount() > 0) {
				Pos_t moveCount = 1 + leftSib.node->OverCount() / 2;
				return moveRight(leftSib, lastInfo, moveCount);
			}
			hasLeftLeftSib = collectLeftSib(leftSib, leftLeftSib);
			if (hasLeftLeftSib && leftLeftSib.node->OverCount() > 0) {
				Pos_t moveCount = 1 + (2 * leftLeftSib.node->OverCount() - 1) / 3;
				moveRight(leftSib, lastInfo, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveRight(leftLeftSib, leftSib, moveCount);
			}
		} else if (hasRightSib) {
			if (rightSib.node->OverCount() > 0) {
				Pos_t moveCount = 1 + rightSib.node->OverCount() / 2;
				return moveLeft(lastInfo, rightSib, moveCount);
			}
			hasRightRightSib = collectRightSib(rightSib, rightRightSib);
			if (hasRightRightSib && rightRightSib.node->OverCount() > 0) {
				Pos_t moveCount = 1 + (2 * rightRightSib.node->OverCount() - 1) / 3;
				moveLeft(lastInfo, rightSib, moveCount);
				moveCount = 1 + moveCount / 2;
				return moveLeft(rightSib, rightRightSib, moveCount);
			}
		}

		if (hasLeftSib && hasRightSib) {
			Pos_t moveCount = (lastInfo.node->ElemCount + 1) / 2;
			moveRight(lastInfo, rightSib, moveCount);
			if (CA_TYPEIS(Node_t, CLeaf))
				moveCount = lastInfo.node->ElemCount;
			else
				moveCount = lastInfo.node->ElemCount + 1;
			moveLeft(leftSib, lastInfo, moveCount);
		} else if (hasLeftSib && hasLeftLeftSib) {
			Pos_t moveCount = (lastInfo.node->ElemCount + 1) / 2;
			moveLeft(leftLeftSib, leftSib, moveCount);
			if (CA_TYPEIS(Node_t, CLeaf))
				moveCount = lastInfo.node->ElemCount;
			else
				moveCount = lastInfo.node->ElemCount + 1;
			moveLeft(leftSib, lastInfo, moveCount);
		} else if (hasRightSib && hasRightRightSib) {
			Pos_t moveCount = (lastInfo.node->ElemCount + 1) / 2;
			moveRight(rightSib, rightRightSib, moveCount);
			if (CA_TYPEIS(Node_t, CLeaf))
				moveCount = lastInfo.node->ElemCount;
			else
				moveCount = lastInfo.node->ElemCount + 1;
			moveRight(lastInfo, rightSib, moveCount);
		} else if (hasLeftSib) {
			if (CA_TYPEIS(Node_t, CLeaf)) {
				if (lastInfo.node->ElemCount + leftSib.node->ElemCount > lastInfo.node->ElemsMaxCount)
					return true;
			} else {
				if (lastInfo.node->ElemCount + leftSib.node->ElemCount > lastInfo.node->ElemsMaxCount - 1)
					return true;
			}
			Pos_t moveCount;
			if (CA_TYPEIS(Node_t, CLeaf))
				moveCount = lastInfo.node->ElemCount;
			else
				moveCount = lastInfo.node->ElemCount + 1;
			moveLeft(leftSib, lastInfo, moveCount);
		} else if (hasRightSib) {
			if (CA_TYPEIS(Node_t, CLeaf)) {
				if (lastInfo.node->ElemCount + rightSib.node->ElemCount > lastInfo.node->ElemsMaxCount)
					return true;
			} else {
				if (lastInfo.node->ElemCount + rightSib.node->ElemCount > lastInfo.node->ElemsMaxCount - 1)
					return true;
			}
			Pos_t moveCount;
			if (CA_TYPEIS(Node_t, CLeaf))
				moveCount = lastInfo.node->ElemCount;
			else
				moveCount = lastInfo.node->ElemCount + 1;
			moveRight(lastInfo, rightSib, moveCount);
		} else {
			if (lastInfo.node->ElemCount > 0)
				return true;
			btree_assert(lastInfo.parentInfo == 0);
			if (CA_TYPEIS(Node_t, CLeaf)) {
				btree_assert(depth == 1);
				btree_assert(count == 0);
				root = 0;
				depth = 0;
				disposeNode(lastInfo.node, lastInfo.nodeId);
				return true;
			} else {
				btree_assert(lastInfo.node->ElemCount == 0);
				btree_assert(depth > 1);
				btree_assert(lastInfo.parentInfo == 0);
				depth--;
				rootId = getFirstNodeChild(lastInfo.node);
				root = getNode(rootId);
				disposeNode(lastInfo.node, lastInfo.nodeId);
				return true;
			}
		}
		btree_assert(lastInfo.node->ElemCount == (CA_TYPEIS(Node_t, CLeaf) ? 0 : Pos_t(-1)));
		disposeNode(lastInfo.node, lastInfo.nodeId);
		btree_assert(lastInfo.parentInfo);
		return processDelete(*lastInfo.parentInfo);
	}

	bool Delete(const Elem_t &elem)
	{
		if (!root)
			return false;
#ifdef WIN32
		CNodeInfo<CInner> pathInfo[20];
		btree_assert(depth < sizeof(pathInfo) / sizeof(pathInfo[0]));
#else
		CNodeInfo<CInner> pathInfo[depth - 1];
#endif
		CNodeInfo<CLeaf> lastInfo;
		bool exact;
		collectPath(elem, pathInfo, lastInfo, exact);

		if (!exact)
			return false;

		return processDelete(lastInfo);
	}

	template<class Key_t>
	Elem_t *Find(const Key_t &key)
	{
		if (!root)
			return 0;
		CNode *node = root;
		bool exact = false;
		for (NodeId_t i = 0; i < depth - 1; i++) {
			CInner *internal = static_cast<CInner *>(node);
			Pos_t pos;
			if (exact)
				pos = internal->ElemCount;
			else
				pos = findInsPoint(internal, key, exact);
			node = getNode(internal->ChildIds[pos]);
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

	size_t Count() const
	{
		return count;
	}

	int checkNode(const CNode *unknode, int level, size_t &calcCount) const
	{
		if (unknode->Type != CT_Leaf && unknode->Type != CT_Inner)
			return 0x8;
		if (unknode->Type == CT_Leaf) {
			calcCount += unknode->ElemCount;
			const CLeaf *node = static_cast<const CLeaf *>(unknode);
			int result = 0;
			if (level != 1)
				result |= 0x10;
			if (node->ElemCount == 0)
				result |= 0x20;
			if (node->ElemCount > node->ElemsMaxCount)
				result |= 0x20;
			for (Pos_t i = 1; i < node->ElemCount; i++)
				if (CComp::Comp(node->Elems[i - 1], node->Elems[i], param) >= 0)
					result |= 0x40;
			return result;
		} else {
			const CInner *node = static_cast<const CInner *>(unknode);
			int result = 0;
			if (node->ElemCount == Pos_t(-1))
				result |= 0x100;
			if (node->ElemCount == 0)
				result |= 0x100;
			if (node->ElemCount > node->ElemsMaxCount)
				result |= 0x100;
			for (Pos_t i = 1; i < node->ElemCount; i++)
				if (CComp::Comp(node->Elems[i - 1], node->Elems[i], param) >= 0)
					result |= 0x200;
			for (Pos_t i = 0; i < node->ElemCount; i++)
				if (node->Elems[i] != getMaxElem(getNode(node->ChildIds[i])))
					result |= 0x400;
			if (CComp::Comp(node->Elems[node->ElemCount - 1], getMaxElem(node), param) >= 0)
				result |= 0x800;
			for (Pos_t i = 0; i < node->ElemCount + 1; i++)
				result |= checkNode(getNode(node->ChildIds[i]), level - 1, calcCount);
			return result;
		}
	}

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
		if (getNode(rootId) != root)
			result |= 0x2;
		size_t calcCount = 0;
		result |= checkNode(root, depth, calcCount);
		if (count != calcCount)
			result |= 0x4;
		return result;
	}

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
		for(Pos_t i = 0; i < node->ElemCount; i++) {
			printIndent(stream, indent);
			stream << node->Elems[i] << "\n";
			next = static_cast<CNode *>(providence.Get(node->ChildIds[i + 1]));
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

	template <class CStream>
	void Print(CStream &stream) const
	{
		if (!root) {
			stream << "Empty\n";
			return;
		}
		print(stream, root);
	}

	CCensoredTree() : root(0), leafCount(0), internalCount(0), garbageCount(0), depth(0), count(0), garbage(0)
	{
		CT_ASSERT(sizeof(CGarbage) <= NodeSize);
		CT_ASSERT(sizeof(CLeaf) <= NodeSize);
		CT_ASSERT(sizeof(CInner) <= NodeSize);
		CT_ASSERT(CInner::ElemsMaxCount >= 4); // for 1/4 of node count not to be zero
	}
	CCensoredTree(const CompPar_t &_param) : root(0), leafCount(0), internalCount(0), garbageCount(0), depth(0), count(0), garbage(0), param(_param)
	{
	}

private:
	CNode *root;
	NodeId_t rootId;
	NodeId_t leafCount, internalCount, garbageCount;
	NodeId_t depth;
	size_t count;
	CGarbage *garbage;
	CompPar_t param;

	CProvidence<CAllocator, NodeId_t, AllocLevel, NodeSize, AllocSize> providence;

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
	Node_t *newNode(NodeId_t &id)
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
		//return providence.Get<CNode>(id);
		return static_cast<CNode *>(providence.Get(id));
	}
	template<class Node_t>
	Node_t *getNode(NodeId_t id) const
	{
		//return providence.Get<Node_t>(id);
		return static_cast<Node_t *>(providence.Get(id));
	}

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
		Elem_t *elem = findInsPoint(node->Elems, node->Elems + node->ElemCount, key, exact);
		return static_cast<Pos_t>(elem - node->Elems);
	}

	void collectPath(const Elem_t &newElem, CNodeInfo<CInner> *pathInfo, CNodeInfo<CLeaf> &lastInfo, bool &exact)
	{
		exact = false;

		CNodeInfo<CInner> *prevInfo = 0;
		Pos_t prevPos = 0;
		CNode *node = root;
		NodeId_t nodeId = rootId;
		Elem_t *maxElemCopy = 0;
		for (NodeId_t i = 0; i < depth - 1; i++) {
			CInner *internal = static_cast<CInner *>(node);
			pathInfo[i].node = internal;
			pathInfo[i].nodeId = nodeId;
			pathInfo[i].parentInfo = prevInfo;
			pathInfo[i].posInParent = prevPos;
			pathInfo[i].maxElemCopy = maxElemCopy;
			Pos_t pos;
			if (exact)
				pos = internal->ElemCount;
			else
				pos = findInsPoint(internal, newElem, exact);
			pathInfo[i].insertionPoint = pos;

			if (pos < internal->ElemCount)
				maxElemCopy = internal->Elems + pos;
			nodeId = internal->ChildIds[pos];
			node = static_cast<CNode *>(providence.Get(nodeId));
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
	bool collectLeftSib(const CNodeInfo<Node_t> &info, CNodeInfo<Node_t> &sibInfo)
	{
		CNodeInfo<CInner> *parentInfo = info.parentInfo;
		if (!parentInfo)
			return false;
		if (info.posInParent == 0)
			return false;

		sibInfo.parentInfo = parentInfo;
		sibInfo.posInParent = info.posInParent - 1;
		sibInfo.nodeId = parentInfo->node->ChildIds[sibInfo.posInParent];
		sibInfo.node = static_cast<Node_t *>(providence.Get(sibInfo.nodeId));
		sibInfo.maxElemCopy = parentInfo->node->Elems + sibInfo.posInParent;
		sibInfo.insertionPoint = Pos_t(-1); // unused
		return true;
	}
	template<class Node_t>
	bool collectRightSib(const CNodeInfo<Node_t> &info, CNodeInfo<Node_t> &sibInfo)
	{
		CNodeInfo<CInner> *parentInfo = info.parentInfo;
		if (!parentInfo)
			return false;
		if (info.posInParent >= parentInfo->node->ElemCount)
			return false;

		sibInfo.parentInfo = parentInfo;
		sibInfo.posInParent = info.posInParent + 1;
		sibInfo.nodeId = parentInfo->node->ChildIds[sibInfo.posInParent];
		sibInfo.node = static_cast<Node_t *>(providence.Get(sibInfo.nodeId));
		if (sibInfo.posInParent >= parentInfo->node->ElemCount)
			sibInfo.maxElemCopy = parentInfo->maxElemCopy;
		else
			sibInfo.maxElemCopy = parentInfo->node->Elems + sibInfo.posInParent;
		sibInfo.insertionPoint = Pos_t(-1); // unused
		return true;
	}
	template<class Node_t>
	void prepareNewSib(const CNodeInfo<Node_t> &info, CNodeInfo<Node_t> &sibInfo, Node_t* newNode, NodeId_t newId)
	{
		CNodeInfo<CInner> *parentInfo = info.parentInfo;

		sibInfo.parentInfo = parentInfo;
		sibInfo.posInParent = info.posInParent + 1;
		sibInfo.nodeId = newId;
		sibInfo.node = newNode;
		sibInfo.maxElemCopy = 0;
		sibInfo.insertionPoint = Pos_t(-1); // unused
	}

};

