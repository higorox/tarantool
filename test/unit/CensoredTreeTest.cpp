#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

void *operator new(size_t, void *buf)
{
	return buf;
}

#include "unit.h"
#include "sptree.h"
#include "CensoredTree.h"
#include "qsort_arg.h"

#ifndef MAX
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif //#ifndef MAX

SPTREE_DEF(test, realloc, qsort_arg);

typedef long type_t;

static int
node_comp(const void *p1, const void *p2, void* unused)
{
	(void)unused;
	return *((const type_t *)p1) < *((const type_t *)p2) ? -1 : *((const type_t *)p2) < *((const type_t *)p1) ? 1 : 0;
}

struct CComparer {
	static int Comp(long a, long b, int unused)
	{
		(void)unused;
		return a < b ? -1 : a > b ? 1 : 0;
	}
};

const int TestAllocSize = 16*1024;
typedef long type_t;
typedef CCensoredTree<type_t, 256, CStupidAllocator<TestAllocSize>, TestAllocSize, 3, CComparer, int> Tree_t;

static void
simple_check()
{
	header();
	
	const int rounds = 10000;
	Tree_t tree;

	// Insert 1..X, remove 1..X
	for (int i = 0; i < rounds; i++) {
		long v = i;
		if (tree.Find(v) != NULL)
			fail("element already in tree (1)", "true");
		tree.Insert(v);
	}
	if (tree.Count() != rounds)
		fail("Tree count mismatch (1)", "true");

	for (int i = 0; i < rounds; i++) {
		long v = i;
		if (tree.Find(v) == NULL)
			fail("element in tree (1)", "false");
		tree.Delete(v);
	}
	if (tree.Count() != 0)
		fail("Tree count mismatch (2)", "true");

	// Insert 1..X, remove X..1
	for (int i = 0; i < rounds; i++) {
		long v = i;
		if (tree.Find(v) != NULL)
			fail("element already in tree (2)", "true");
		tree.Insert(v);
	}
	if (tree.Count() != rounds)
		fail("Tree count mismatch (3)", "true");

	for (int i = 0; i < rounds; i++) {
		long v = rounds - 1 - i;
		if (tree.Find(v) == NULL)
			fail("element in tree (2)", "false");
		tree.Delete(v);
	}
	if (tree.Count() != 0)
		fail("Tree count mismatch (4)", "true");

	// Insert X..1, remove 1..X
	for (int i = 0; i < rounds; i++) {
		long v = rounds - 1 - i;
		if (tree.Find(v) != NULL)
			fail("element already in tree (3)", "true");
		tree.Insert(v);
	}
	if (tree.Count() != rounds)
		fail("Tree count mismatch (5)", "true");

	for (int i = 0; i < rounds; i++) {
		long v = i;
		if (tree.Find(v) == NULL)
			fail("element in tree (3)", "false");
		tree.Delete(v);
	}
	if (tree.Count() != 0)
		fail("Tree count mismatch (6)", "true");

	// Insert X..1, remove X..1
	for (int i = 0; i < rounds; i++) {
		long v = rounds - 1 - i;
		if (tree.Find(v) != NULL)
			fail("element already in tree (4)", "true");
		tree.Insert(v);
	}
	if (tree.Count() != rounds)
		fail("Tree count mismatch (7)", "true");

	for (int i = 0; i < rounds; i++) {
		long v = rounds - 1 - i;
		if (tree.Find(v) == NULL)
			fail("element in tree (4)", "false");
		tree.Delete(v);
	}
	if (tree.Count() != 0)
		fail("Tree count mismatch (8)", "true");

	footer();
}

static void
compare_with_sptree_check()
{
	header();

	sptree_test spt_test;
	sptree_test_init(&spt_test, sizeof(type_t), 0, 0, 0, &node_comp, 0, 0);

	Tree_t tree;
	
	const int rounds = 256 * 1024;
	const int elem_limit = 32 * 1024;

	for (int i = 0; i < rounds; i++) {
		long rnd = rand() % elem_limit;
		int find_res1 = sptree_test_find(&spt_test, &rnd) ? 1 : 0;
		int find_res2 = tree.Find(rnd) ? 1 : 0;
		if (find_res1 ^ find_res2) {
			fail("trees identity", "false");
			continue;
		}
			
		if (find_res1 == 0) {
			sptree_test_replace(&spt_test, &rnd, NULL);
			tree.Insert(rnd);
			
		} else {
			sptree_test_delete(&spt_test, &rnd);
			tree.Delete(rnd);
		}
	}
	sptree_test_destroy(&spt_test);
	
	footer();
}

int
main(void)
{
	simple_check();
	compare_with_sptree_check();
}
