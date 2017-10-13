/*
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
 *
 * Copyright (c) 2006 Maxim Yegorushkin <maxim.yegorushkin@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>


#include "ae.h"

static void min_heap_ctor(min_heap_t *s);
static void min_heap_dtor(min_heap_t *s);
static void min_heap_elem_init(aeTimeEvent *e);
static int min_heap_elem_greater(aeTimeEvent *a, aeTimeEvent *b);
static int min_heap_push(min_heap_t *s, aeTimeEvent *e);
static int min_heap_reserve(min_heap_t *s, unsigned int n);
static void min_heap_shift_up_(min_heap_t *s, unsigned hole_index, aeTimeEvent *e);
static void min_heap_shift_down_(min_heap_t *s, unsigned hole_index, aeTimeEvent *e);

static int min_heap_elem_greater(aeTimeEvent *a, aeTimeEvent *b)
{
	if (a->when_sec > b->when_sec ||
		(a->when_sec == b->when_sec && a->when_ms > b->when_ms))
		return 1;
	return 0;		
}

static void min_heap_ctor(min_heap_t *s) 
{ 
	s->p = NULL; 
	s->n = 0; 
	s->a = 0; 
}

static void min_heap_dtor(min_heap_t *s) 
{
	if (s->p) 
		free(s->p); 
}

static void min_heap_elem_init(aeTimeEvent *e) 
{ 
	e->min_heap_idx = -1; 
}

int min_heap_empty(min_heap_t *s) 
{ 	
	return 0u == s->n; 
}

unsigned min_heap_size(min_heap_t *s) 
{ 
	return s->n; 
}

//取得根节点
aeTimeEvent *min_heap_top(min_heap_t *s) 
{ 
	return s->n ? *s->p : NULL; 
}

//插入一个节点
static int min_heap_push(min_heap_t *s, aeTimeEvent *e)
{
	if (min_heap_reserve(s, s->n + 1))
		return -1;

	min_heap_shift_up_(s, s->n++, e);

	return 0;
}

//pop stack
//删除根节点
aeTimeEvent *min_heap_pop(min_heap_t *s)
{
	if (s->n) {
		aeTimeEvent *e = *s->p;
		//最小值的索引号一定是0
		//取得s->p[--s->n]为最末尾的节点
		min_heap_shift_down_(s, 0u, s->p[--s->n]);
		e->min_heap_idx = -1;
		return e;
	}

	return NULL;
}

int min_heap_elt_is_top(const aeTimeEvent *e)
{
	return e->min_heap_idx == 0;
}
//删除指定的节点
int min_heap_erase(min_heap_t *s, aeTimeEvent *e)
{
	if (e->min_heap_idx != -1) {
		//取得最后一个节点
		aeTimeEvent *last = s->p[--s->n];
		unsigned parent = (e->min_heap_idx - 1) / 2;
		/* we replace e with the last element in the heap.  We might need to
		   shift it upward if it is less than its parent, or downward if it is
		   greater than one or both its children. Since the children are known
		   to be less than the parent, it can't need to shift both up and
		   down. */
		//1.
		//2.e->min_heap_idx == 0，向下移动

		if (e->min_heap_idx > 0 && min_heap_elem_greater(s->p[parent], last))
			min_heap_shift_up_(s, e->min_heap_idx, last);
		else
			min_heap_shift_down_(s, e->min_heap_idx, last);
		e->min_heap_idx = -1;
		return 0;
	}

	return -1;
}

// n = 1; check and malloc room 
static int min_heap_reserve(min_heap_t *s, unsigned int n)
{
	if (s->a < n) {
		aeTimeEvent **p;
		unsigned int a = s->a ? s->a * 2 : 32;
		if (a < n)
			a = n;
		if (!(p = (aeTimeEvent **)realloc(s->p, a * sizeof (*p))))
			return -1;
		s->p = p;
		s->a = a;
	}
	return 0;
}

//将节点e插入hole_index出
static void min_heap_shift_up_(min_heap_t *s, unsigned hole_index, aeTimeEvent *e)
{
	//求得父节点的索引号
	unsigned parent = (hole_index - 1) / 2;
	//1.如果插入位置的索引号hole_index为0，表示已经到根节点，无需再比较了
	//2.如果父节点的值大于插入节点的值，交换节点
	while (hole_index && min_heap_elem_greater(s->p[parent], e)) {
		//移动父节点
		(s->p[hole_index] = s->p[parent])->min_heap_idx = hole_index;
		//再次求得父节点的索引号
		//以及插入位置的索引号
		hole_index = parent;
		parent = (hole_index - 1) / 2;
	}
	/* save the pointer */
	//记录索引号和指针
	(s->p[hole_index] = e)->min_heap_idx = hole_index;
}

//将节点e插入hole_index出
static void min_heap_shift_down_(min_heap_t *s, unsigned int hole_index, aeTimeEvent *e)
{
	//取得右节点的索引值
	unsigned int min_child = 2 * (hole_index + 1);
	//向下移动节点
	//min_child > n，表示已经到末尾了
	while (min_child <= s->n) {
		//相对于hole_index， s->p[min_child]为右节点，s->p[min_child - 1]为左节点
		min_child -= min_child == s->n || min_heap_elem_greater(s->p[min_child], s->p[min_child - 1]);
		//如果e 节点小于节点，结束
		if (!min_heap_elem_greater(e, s->p[min_child]))
			break;
		//交换节点
		(s->p[hole_index] = s->p[min_child])->min_heap_idx = hole_index;
		hole_index = min_child;
		min_child = 2 * (hole_index + 1);
	}
	//插入节点
	(s->p[hole_index] = e)->min_heap_idx = hole_index;
}

min_heap_t *min_heap_init(min_heap_t *heap)
{
	if (!heap)
		heap = (min_heap_t *)malloc(sizeof(*heap));
	if (heap)
		min_heap_ctor(heap);

	return heap;
}

void min_heap_destroy(min_heap_t *heap)
{
	if (heap)
		min_heap_dtor(heap);
}

int aetimer_event_add(min_heap_t *s, aeTimeEvent *te)
{
	min_heap_elem_init(te);

	return min_heap_push(s, te);
}

void aetimer_event_init(aeTimeEvent *te)
{
	min_heap_elem_init(te);
}
