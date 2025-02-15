/*
 * include/common/mini-clist.h
 * Circular list manipulation macros and structures.
 *
 * Copyright (C) 2002-2014 Willy Tarreau - w@1wt.eu
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, version 2.1
 * exclusively.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _COMMON_MINI_CLIST_H
#define _COMMON_MINI_CLIST_H

#include <common/config.h>

/* these are circular or bidirectionnal lists only. Each list pointer points to
 * another list pointer in a structure, and not the structure itself. The
 * pointer to the next element MUST be the first one so that the list is easily
 * cast as a single linked list or pointer.
 */
struct list {
    struct list *n;	/* next */
    struct list *p;	/* prev */
};

/* This is similar to struct list, but we want to be sure the compiler will
 * yell at you if you use macroes for one when you're using the other. You have
 * to expicitely cast if that's really what you want to do.
 */
struct mt_list {
    struct mt_list *next;
    struct mt_list *prev;
};


/* a back-ref is a pointer to a target list entry. It is used to detect when an
 * element being deleted is currently being tracked by another user. The best
 * example is a user dumping the session table. The table does not fit in the
 * output buffer so we have to set a mark on a session and go on later. But if
 * that marked session gets deleted, we don't want the user's pointer to go in
 * the wild. So we can simply link this user's request to the list of this
 * session's users, and put a pointer to the list element in ref, that will be
 * used as the mark for next iteration.
 */
struct bref {
	struct list users;
	struct list *ref; /* pointer to the target's list entry */
};

/* a word list is a generic list with a pointer to a string in each element. */
struct wordlist {
	struct list list;
	char *s;
};

/* this is the same as above with an additional pointer to a condition. */
struct cond_wordlist {
	struct list list;
	void *cond;
	char *s;
};

/* First undefine some macros which happen to also be defined on OpenBSD,
 * in sys/queue.h, used by sys/event.h
 */
#undef LIST_HEAD
#undef LIST_INIT
#undef LIST_NEXT

/* ILH = Initialized List Head : used to prevent gcc from moving an empty
 * list to BSS. Some older version tend to trim all the array and cause
 * corruption.
 */
#define ILH		{ .n = (struct list *)1, .p = (struct list *)2 }

#define LIST_HEAD(a)	((void *)(&(a)))

#define LIST_INIT(l) ((l)->n = (l)->p = (l))

#define LIST_HEAD_INIT(l) { &l, &l }

/* adds an element at the beginning of a list ; returns the element */
#define LIST_ADD(lh, el) ({ (el)->n = (lh)->n; (el)->n->p = (lh)->n = (el); (el)->p = (lh); (el); })

/* adds an element at the end of a list ; returns the element */
#define LIST_ADDQ(lh, el) ({ (el)->p = (lh)->p; (el)->p->n = (lh)->p = (el); (el)->n = (lh); (el); })

/* adds the contents of a list <old> at the beginning of another list <new>. The old list head remains untouched. */
#define LIST_SPLICE(new, old) do {				     \
		if (!LIST_ISEMPTY(old)) {			     \
			(old)->p->n = (new)->n; (old)->n->p = (new); \
			(new)->n->p = (old)->p; (new)->n = (old)->n; \
		}						     \
	} while (0)

/* removes an element from a list and returns it */
#define LIST_DEL(el) ({ typeof(el) __ret = (el); (el)->n->p = (el)->p; (el)->p->n = (el)->n; (__ret); })

/* removes an element from a list, initializes it and returns it.
 * This is faster than LIST_DEL+LIST_INIT as we avoid reloading the pointers.
 */
#define LIST_DEL_INIT(el) ({ \
	typeof(el) __ret = (el);                        \
	typeof(__ret->n) __n = __ret->n;                \
	typeof(__ret->p) __p = __ret->p;                \
	__n->p = __p; __p->n = __n;                     \
	__ret->n = __ret->p = __ret;                    \
	__ret;                                          \
})

/* returns a pointer of type <pt> to a structure containing a list head called
 * <el> at address <lh>. Note that <lh> can be the result of a function or macro
 * since it's used only once.
 * Example: LIST_ELEM(cur_node->args.next, struct node *, args)
 */
#define LIST_ELEM(lh, pt, el) ((pt)(((void *)(lh)) - ((void *)&((pt)NULL)->el)))

/* checks if the list head <lh> is empty or not */
#define LIST_ISEMPTY(lh) ((lh)->n == (lh))

/* checks if the list element <el> was added to a list or not. This only
 * works when detached elements are reinitialized (using LIST_DEL_INIT)
 */
#define LIST_ADDED(el) ((el)->n != (el))

/* returns a pointer of type <pt> to a structure following the element
 * which contains list head <lh>, which is known as element <el> in
 * struct pt.
 * Example: LIST_NEXT(args, struct node *, list)
 */
#define LIST_NEXT(lh, pt, el) (LIST_ELEM((lh)->n, pt, el))


/* returns a pointer of type <pt> to a structure preceding the element
 * which contains list head <lh>, which is known as element <el> in
 * struct pt.
 */
#undef LIST_PREV
#define LIST_PREV(lh, pt, el) (LIST_ELEM((lh)->p, pt, el))

/*
 * Simpler FOREACH_ITEM macro inspired from Linux sources.
 * Iterates <item> through a list of items of type "typeof(*item)" which are
 * linked via a "struct list" member named <member>. A pointer to the head of
 * the list is passed in <list_head>. No temporary variable is needed. Note
 * that <item> must not be modified during the loop.
 * Example: list_for_each_entry(cur_acl, known_acl, list) { ... };
 */ 
#define list_for_each_entry(item, list_head, member)                      \
	for (item = LIST_ELEM((list_head)->n, typeof(item), member);     \
	     &item->member != (list_head);                                \
	     item = LIST_ELEM(item->member.n, typeof(item), member))

/*
 * Same as list_for_each_entry but starting from current point
 * Iterates <item> through the list starting from <item>
 * It's basically the same macro but without initializing item to the head of
 * the list.
 */
#define list_for_each_entry_from(item, list_head, member) \
	for ( ; &item->member != (list_head); \
	     item = LIST_ELEM(item->member.n, typeof(item), member))

/*
 * Simpler FOREACH_ITEM_SAFE macro inspired from Linux sources.
 * Iterates <item> through a list of items of type "typeof(*item)" which are
 * linked via a "struct list" member named <member>. A pointer to the head of
 * the list is passed in <list_head>. A temporary variable <back> of same type
 * as <item> is needed so that <item> may safely be deleted if needed.
 * Example: list_for_each_entry_safe(cur_acl, tmp, known_acl, list) { ... };
 */ 
#define list_for_each_entry_safe(item, back, list_head, member)           \
	for (item = LIST_ELEM((list_head)->n, typeof(item), member),     \
	     back = LIST_ELEM(item->member.n, typeof(item), member);     \
	     &item->member != (list_head);                                \
	     item = back, back = LIST_ELEM(back->member.n, typeof(back), member))


/*
 * Same as list_for_each_entry_safe but starting from current point
 * Iterates <item> through the list starting from <item>
 * It's basically the same macro but without initializing item to the head of
 * the list.
 */
#define list_for_each_entry_safe_from(item, back, list_head, member) \
	for (back = LIST_ELEM(item->member.n, typeof(item), member);     \
	     &item->member != (list_head);                                \
	     item = back, back = LIST_ELEM(back->member.n, typeof(back), member))

#include <common/hathreads.h>
#define MT_LIST_BUSY ((struct mt_list *)1)

/*
 * Locked version of list manipulation macros.
 * It is OK to use those concurrently from multiple threads, as long as the
 * list is only used with the locked variants.
 */

/*
 * Add an item at the beginning of a list.
 * Returns 1 if we added the item, 0 otherwise (because it was already in a
 * list).
 */
#define MT_LIST_ADD(lh, el)                                                \
     ({                                                                    \
        int _ret = 0;                                                      \
	do {                                                               \
		while (1) {                                                \
			struct mt_list *n;                                 \
			struct mt_list *p;                                 \
			n = _HA_ATOMIC_XCHG(&(lh)->next, MT_LIST_BUSY);      \
			if (n == MT_LIST_BUSY)                               \
			        continue;                                  \
			p = _HA_ATOMIC_XCHG(&n->prev, MT_LIST_BUSY);         \
			if (p == MT_LIST_BUSY) {                             \
				(lh)->next = n;                            \
				__ha_barrier_store();                      \
				continue;                                  \
			}                                                  \
			if ((el)->next != (el) || (el)->prev != (el)) {    \
				(n)->prev = p;                             \
				(lh)->next = n;                            \
				break;                                     \
			}                                                  \
			(el)->next = n;                                    \
			(el)->prev = p;                                    \
			__ha_barrier_store();                              \
			n->prev = (el);                                    \
			__ha_barrier_store();                              \
			p->next = (el);                                    \
			__ha_barrier_store();                              \
			_ret = 1;                                          \
			break;                                             \
		}                                                          \
	} while (0);                                                       \
	(_ret);                                                            \
     })

/*
 * Add an item at the end of a list.
 * Returns 1 if we added the item, 0 otherwise (because it was already in a
 * list).
 */
#define MT_LIST_ADDQ(lh, el)                                               \
    ({                                                                     \
	    int _ret = 0;                                   \
	do {                                                               \
		while (1) {                                                \
			struct mt_list *n;                                 \
			struct mt_list *p;                                 \
			p = _HA_ATOMIC_XCHG(&(lh)->prev, MT_LIST_BUSY);      \
			if (p == MT_LIST_BUSY)                               \
			        continue;                                  \
			n = _HA_ATOMIC_XCHG(&p->next, MT_LIST_BUSY);         \
			if (n == MT_LIST_BUSY) {                             \
				(lh)->prev = p;                            \
				__ha_barrier_store();                      \
				continue;                                  \
			}                                                  \
			if ((el)->next != (el) || (el)->prev != (el)) {    \
				p->next = n;                               \
				(lh)->prev = p;                            \
				break;                                     \
			}                                                  \
			(el)->next = n;                                    \
			(el)->prev = p;                                    \
			__ha_barrier_store();                              \
			p->next = (el);                                    \
			__ha_barrier_store();                              \
			n->prev = (el);                                    \
			__ha_barrier_store();                              \
			_ret = 1;                                          \
			break;                                             \
		}                                                          \
	} while (0);                                                       \
	(_ret);                                                            \
    })

/* Remove an item from a list.
 * Returns 1 if we removed the item, 0 otherwise (because it was in no list).
 */
#define MT_LIST_DEL(el)                                                    \
    ({                                                                     \
        int _ret = 0;                                                      \
	do {                                                               \
		while (1) {                                                \
			struct mt_list *n, *n2;                            \
			struct mt_list *p, *p2 = NULL;                     \
			n = _HA_ATOMIC_XCHG(&(el)->next, MT_LIST_BUSY);      \
			if (n == MT_LIST_BUSY)                               \
			        continue;                                  \
			p = _HA_ATOMIC_XCHG(&(el)->prev, MT_LIST_BUSY);      \
			if (p == MT_LIST_BUSY) {                             \
				(el)->next = n;                            \
				__ha_barrier_store();                      \
				continue;                                  \
			}                                                  \
			if (p != (el)) {                                   \
			        p2 = _HA_ATOMIC_XCHG(&p->next, MT_LIST_BUSY);\
			        if (p2 == MT_LIST_BUSY) {                    \
			                (el)->prev = p;                    \
					(el)->next = n;                    \
					__ha_barrier_store();              \
					continue;                          \
				}                                          \
			}                                                  \
			if (n != (el)) {                                   \
			        n2 = _HA_ATOMIC_XCHG(&n->prev, MT_LIST_BUSY);\
				if (n2 == MT_LIST_BUSY) {                    \
					if (p2 != NULL)                    \
						p->next = p2;              \
					(el)->prev = p;                    \
					(el)->next = n;                    \
					__ha_barrier_store();              \
					continue;                          \
				}                                          \
			}                                                  \
			n->prev = p;                                       \
			p->next = n;                                       \
			if (p != (el) && n != (el))                        \
				_ret = 1;                                  \
			__ha_barrier_store();                              \
			(el)->prev = (el);                                 \
			(el)->next = (el);                                 \
			__ha_barrier_store();                              \
			break;                                             \
		}                                                          \
	} while (0);                                                       \
	(_ret);                                                            \
    })


/* Remove the first element from the list, and return it */
#define MT_LIST_POP(lh, pt, el)                                            \
	({                                                                 \
		 void *_ret;                                               \
		 while (1) {                                               \
			 struct mt_list *n, *n2;                           \
			 struct mt_list *p, *p2;                           \
			 n = _HA_ATOMIC_XCHG(&(lh)->next, MT_LIST_BUSY);     \
			 if (n == MT_LIST_BUSY)                              \
			         continue;                                 \
			 if (n == (lh)) {                                  \
				 (lh)->next = lh;                          \
				 __ha_barrier_store();                     \
				 _ret = NULL;                              \
				 break;                                    \
			 }                                                 \
			 p = _HA_ATOMIC_XCHG(&n->prev, MT_LIST_BUSY);        \
			 if (p == MT_LIST_BUSY) {                            \
				 (lh)->next = n;                           \
				 __ha_barrier_store();                     \
				 continue;                                 \
			 }                                                 \
			 n2 = _HA_ATOMIC_XCHG(&n->next, MT_LIST_BUSY);       \
			 if (n2 == MT_LIST_BUSY) {                           \
				 n->prev = p;                              \
				 __ha_barrier_store();                     \
				 (lh)->next = n;                           \
				 __ha_barrier_store();                     \
				 continue;                                 \
			 }                                                 \
			 p2 = _HA_ATOMIC_XCHG(&n2->prev, MT_LIST_BUSY);      \
			 if (p2 == MT_LIST_BUSY) {                           \
				 n->next = n2;                             \
				 n->prev = p;                              \
				 __ha_barrier_store();                     \
				 (lh)->next = n;                           \
				 __ha_barrier_store();                     \
				 continue;                                 \
			 }                                                 \
			 (lh)->next = n2;                                  \
			 (n2)->prev = (lh);                                \
			 __ha_barrier_store();                             \
			 (n)->prev = (n);                                  \
			 (n)->next = (n);	                           \
			 __ha_barrier_store();                             \
			 _ret = MT_LIST_ELEM(n, pt, el);                   \
			 break;                                            \
		 }                                                         \
		 (_ret);                                                   \
	 })

#define MT_LIST_HEAD(a)	((void *)(&(a)))

#define MT_LIST_INIT(l) ((l)->next = (l)->prev = (l))

#define MT_LIST_HEAD_INIT(l) { &l, &l }
/* returns a pointer of type <pt> to a structure containing a list head called
 * <el> at address <lh>. Note that <lh> can be the result of a function or macro
 * since it's used only once.
 * Example: MT_LIST_ELEM(cur_node->args.next, struct node *, args)
 */
#define MT_LIST_ELEM(lh, pt, el) ((pt)(((void *)(lh)) - ((void *)&((pt)NULL)->el)))

/* checks if the list head <lh> is empty or not */
#define MT_LIST_ISEMPTY(lh) ((lh)->next == (lh))

/* returns a pointer of type <pt> to a structure following the element
 * which contains list head <lh>, which is known as element <el> in
 * struct pt.
 * Example: MT_LIST_NEXT(args, struct node *, list)
 */
#define MT_LIST_NEXT(lh, pt, el) (MT_LIST_ELEM((lh)->next, pt, el))


/* returns a pointer of type <pt> to a structure preceding the element
 * which contains list head <lh>, which is known as element <el> in
 * struct pt.
 */
#undef MT_LIST_PREV
#define MT_LIST_PREV(lh, pt, el) (MT_LIST_ELEM((lh)->prev, pt, el))

/* checks if the list element <el> was added to a list or not. This only
 * works when detached elements are reinitialized (using LIST_DEL_INIT)
 */
#define MT_LIST_ADDED(el) ((el)->next != (el))

/* Lock an element in the list, to be sure it won't be removed.
 * It needs to be synchronized somehow to be sure it's not removed
 * from the list in the meanwhile.
 * This returns a struct mt_list, that will be needed at unlock time.
 */
#define MT_LIST_LOCK_ELT(el)                                               \
	({                                                                 \
		struct mt_list ret;                                        \
		while (1) {                                                \
			struct mt_list *n, *n2;                            \
			struct mt_list *p, *p2 = NULL;                     \
			n = _HA_ATOMIC_XCHG(&(el)->next, MT_LIST_BUSY);      \
			if (n == MT_LIST_BUSY)                               \
			        continue;                                  \
			p = _HA_ATOMIC_XCHG(&(el)->prev, MT_LIST_BUSY);      \
			if (p == MT_LIST_BUSY) {                             \
				(el)->next = n;                            \
				__ha_barrier_store();                      \
				continue;                                  \
			}                                                  \
			if (p != (el)) {                                   \
			        p2 = _HA_ATOMIC_XCHG(&p->next, MT_LIST_BUSY);\
			        if (p2 == MT_LIST_BUSY) {                    \
			                (el)->prev = p;                    \
					(el)->next = n;                    \
					__ha_barrier_store();              \
					continue;                          \
				}                                          \
			}                                                  \
			if (n != (el)) {                                   \
			        n2 = _HA_ATOMIC_XCHG(&n->prev, MT_LIST_BUSY);\
				if (n2 == MT_LIST_BUSY) {                    \
					if (p2 != NULL)                    \
						p->next = p2;              \
					(el)->prev = p;                    \
					(el)->next = n;                    \
					__ha_barrier_store();              \
					continue;                          \
				}                                          \
			}                                                  \
			ret.next = n;                                      \
			ret.prev = p;                                      \
			break;                                             \
		}                                                          \
		ret;                                                       \
	})

/* Unlock an element previously locked by MT_LIST_LOCK_ELT. "np" is the
 * struct mt_list returned by MT_LIST_LOCK_ELT().
 */
#define MT_LIST_UNLOCK_ELT(el, np)                                         \
	do {                                                               \
		struct mt_list *n = (np).next, *p = (np).prev;             \
		(el)->next = n;                                            \
		(el)->prev = p;                                            \
		if (n != (el))                                             \
			n->prev = (el);                                    \
		if (p != (el))                                             \
			p->next = (el);                                    \
	} while (0)

/* Internal macroes for the foreach macroes */
#define _MT_LIST_UNLOCK_NEXT(el, np)                                       \
	do {                                                               \
		struct mt_list *n = (np);                                  \
		(el)->next = n;                                            \
		if (n != (el))                                             \
		        n->prev = (el);                                    \
	} while (0)

/* Internal macroes for the foreach macroes */
#define _MT_LIST_UNLOCK_PREV(el, np)                                       \
	do {                                                               \
		struct mt_list *p = (np);                                  \
		(el)->prev = p;                                            \
		if (p != (el))                                             \
		        p->next = (el);                                    \
	} while (0)

#define _MT_LIST_LOCK_NEXT(el)                                             \
	({                                                                 \
	        struct mt_list *n = NULL;                                  \
		while (1) {                                                \
			struct mt_list *n2;                                \
			n = _HA_ATOMIC_XCHG(&((el)->next), MT_LIST_BUSY);    \
			if (n == MT_LIST_BUSY)                               \
			        continue;                                  \
			if (n != (el)) {                                   \
			        n2 = _HA_ATOMIC_XCHG(&n->prev, MT_LIST_BUSY);\
				if (n2 == MT_LIST_BUSY) {                    \
					(el)->next = n;                    \
					__ha_barrier_store();              \
					continue;                          \
				}                                          \
			}                                                  \
			break;                                             \
		}                                                          \
		n;                                                         \
	})

#define _MT_LIST_LOCK_PREV(el)                                             \
	({                                                                 \
	        struct mt_list *p = NULL;                                  \
		while (1) {                                                \
			struct mt_list *p2;                                \
			p = _HA_ATOMIC_XCHG(&((el)->prev), MT_LIST_BUSY);    \
			if (p == MT_LIST_BUSY)                               \
			        continue;                                  \
			if (p != (el)) {                                   \
			        p2 = _HA_ATOMIC_XCHG(&p->next, MT_LIST_BUSY);\
				if (p2 == MT_LIST_BUSY) {                    \
					(el)->prev = p;                    \
					__ha_barrier_store();              \
					continue;                          \
				}                                          \
			}                                                  \
			break;                                             \
		}                                                          \
		p;                                                         \
	})

#define _MT_LIST_RELINK_DELETED(elt2)                                      \
    do {                                                                   \
	    struct mt_list *n = elt2.next, *p = elt2.prev;                 \
	    n->prev = p;                                                   \
	    p->next = n;                                                   \
    } while (0);

/* Equivalent of MT_LIST_DEL(), to be used when parsing the list with mt_list_entry_for_each_safe().
 * It should be the element currently parsed (tmpelt1)
 */
#define MT_LIST_DEL_SAFE(el)                                               \
	do {                                                               \
		(el)->prev = (el);                                         \
		(el)->next = (el);                                         \
		(el) = NULL;                                               \
	} while (0)

/* Simpler FOREACH_ITEM_SAFE macro inspired from Linux sources.
 * Iterates <item> through a list of items of type "typeof(*item)" which are
 * linked via a "struct list" member named <member>. A pointer to the head of
 * the list is passed in <list_head>. A temporary variable <back> of same type
 * as <item> is needed so that <item> may safely be deleted if needed.
 * tmpelt1 is a temporary struct mt_list *, and tmpelt2 is a temporary
 * struct mt_list, used internally, both are needed for MT_LIST_DEL_SAFE.
 * Example: list_for_each_entry_safe(cur_acl, tmp, known_acl, list, elt1, elt2)
 * { ... };
 * If you want to remove the current element, please use MT_LIST_DEL_SAFE.
 */
#define mt_list_for_each_entry_safe(item, list_head, member, tmpelt, tmpelt2)           \
        for ((tmpelt) = NULL; (tmpelt) != MT_LIST_BUSY; ({                    \
					if (tmpelt) {                         \
					if (tmpelt2.prev)                     \
						MT_LIST_UNLOCK_ELT(tmpelt, tmpelt2);           \
					else                                  \
						_MT_LIST_UNLOCK_NEXT(tmpelt, tmpelt2.next); \
				} else                                        \
				_MT_LIST_RELINK_DELETED(tmpelt2);             \
				(tmpelt) = MT_LIST_BUSY;                      \
				}))                                            \
	for ((tmpelt) = (list_head), (tmpelt2).prev = NULL, (tmpelt2).next = _MT_LIST_LOCK_NEXT(list_head); ({ \
	              (item) = MT_LIST_ELEM((tmpelt2.next), typeof(item), member);  \
		      if (&item->member != (list_head)) {                     \
		                if (tmpelt2.prev != &item->member)            \
					tmpelt2.next = _MT_LIST_LOCK_NEXT(&item->member); \
				else \
					tmpelt2.next = tmpelt;                \
				if (tmpelt != NULL) {                         \
					if (tmpelt2.prev)                     \
						_MT_LIST_UNLOCK_PREV(tmpelt, tmpelt2.prev); \
					tmpelt2.prev = tmpelt;                \
				}                                             \
				(tmpelt) = &item->member;                     \
			}                                                     \
	    }),                                                               \
	     &item->member != (list_head);)
#endif /* _COMMON_MINI_CLIST_H */
