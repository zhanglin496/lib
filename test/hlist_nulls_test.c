#include <stdio.h>
/*
 * Special version of lists, where end of list is not a NULL pointer,
 * but a 'nulls' marker, which can have many different values.
 * (up to 2^31 different values guaranteed on all platforms)
 *
 * In the standard hlist, termination of a list is the NULL pointer.
 * In this special 'nulls' variant, we use the fact that objects stored in
 * a list are aligned on a word (4 or 8 bytes alignment).
 * We therefore use the last significant bit of 'ptr' :
 * Set to 1 : This is a 'nulls' end-of-list marker (ptr >> 1)
 * Set to 0 : This is a pointer to some object (ptr)
 */

struct hlist_nulls_head {
	struct hlist_nulls_node *first;
};

struct hlist_nulls_node {
	struct hlist_nulls_node *next, **pprev;
};
//于是nulls hlist的结尾节点的next字段可以编码为高31位和低1位，
//如果低1位为1，那么高31位便可以取出当初存进去的任意值，是不是很精妙呢？！
//之所以可以这么做，原因很简单，在计算机中，
//Linux内核数据结构的所有的地址都是对齐存放的，
//因此最低1位的数据位是空闲的，当然可以借为它用了。
//高31位表示节点值，低1位表示这是一个null节点
#define NULLS_MARKER(value) (1UL | (((long)value) << 1))
#define INIT_HLIST_NULLS_HEAD(ptr, nulls) \
	((ptr)->first = (struct hlist_nulls_node *) NULLS_MARKER(nulls))

#define hlist_nulls_entry(ptr, type, member) container_of(ptr,type,member)
/**
 * ptr_is_a_nulls - Test if a ptr is a nulls
 * @ptr: ptr to be tested
 *
 */
 //如果最低位为1，表示是一个null节点
static inline int is_a_nulls(const struct hlist_nulls_node *ptr)
{
	return ((unsigned long)ptr & 1);
}

/**
 * get_nulls_value - Get the 'nulls' value of the end of chain
 * @ptr: end of chain
 *
 * Should be called only if is_a_nulls(ptr);
 */
 //
//如果最低位为1，表示是一个null节点,获取null节点值
static inline unsigned long get_nulls_value(const struct hlist_nulls_node *ptr)
{
	return ((unsigned long)ptr) >> 1;
}

static inline int hlist_nulls_unhashed(const struct hlist_nulls_node *h)
{
	return !h->pprev;
}

//如果是null节点，表示hash桶是空
static inline int hlist_nulls_empty(const struct hlist_nulls_head *h)
{
	return is_a_nulls(h->first);
}



static inline void hlist_nulls_add_head(struct hlist_nulls_node *n,
					struct hlist_nulls_head *h)
{
	//添加第一个节点，first的值已经在初始的时候初始成对应的null节点值
	struct hlist_nulls_node *first = h->first;
	//添加第一个节点，n的next值为null节点值
	n->next = first;
	//n的pprev指向头结点,实际就是hlist_nulls_head对象的地址
	n->pprev = &h->first;
	//头结点指向n
	h->first = n;
	//添加第一个节点时，first是null节点值，没有指向有效的内存地址
	//所以这里要检查first是否为null节点
	if (!is_a_nulls(first))
		//实际就是hlist_nulls_node对象的地址
		first->pprev = &n->next;
}

static inline void __hlist_nulls_del(struct hlist_nulls_node *n)
{
	struct hlist_nulls_node *next = n->next;
	struct hlist_nulls_node **pprev = n->pprev;
	*pprev = next;
	if (!is_a_nulls(next))
		next->pprev = pprev;
}


int main()
{
	struct hlist_nulls_head head;
	struct hlist_nulls_node n1, n2;
	INIT_HLIST_NULLS_HEAD(&head, 0);
	printf("first=%p,null value=%08lx,head=%p\n", head.first, get_nulls_value(head.first), &head);
	printf("n1=%p,n2=%p\n", &n1, &n2);
	hlist_nulls_add_head(&n1, &head);
	printf("n1->next=%p,n1.pprev=%p\n", n1.next, n1.pprev);
	hlist_nulls_add_head(&n2, &head);
	printf("n2->next=%p,n2.pprev=%p\n", n2.next, n2.pprev);
	printf("n1->next=%p,n1.pprev=%p\n", n1.next, n1.pprev);
}

