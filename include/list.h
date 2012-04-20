#ifndef __LIST_H__
#define __LIST_H__

#include <types.h>
#include <matrix/matrix.h>

/* Doubly linked list node structure */
struct list {
	struct list *prev;
	struct list *next;
};
typedef struct list list_t;


#define LIST_ENTRY(entry, type, member) \
	(type *)((char *)entry - offsetof(type, member))

#define LIST_EMPTY(list) \
	(((list)->prev == (list)) && ((list)->next == (list)))

#define LIST_INIT(list) \
	(((list)->prev) = ((list)->next) = list)

static INLINE void __list_add(struct list *new, struct list *prev,
			      struct list *next)
{
	next->prev = new;
	prev->next = new;
	new->next = next;
	new->prev = prev;
}

static INLINE void list_add(struct list *new, struct list *head)
{
	__list_add(new, head, head->next);
}

static INLINE void list_add_tail(struct list *new, struct list *head)
{
	__list_add(new, head->prev, head);
}

static INLINE void __list_del(struct list *prev, struct list *next)
{
	next->prev = prev;
	prev->next = next;
}

static INLINE void list_del(struct list *entry)
{
	__list_del(entry->prev, entry->next);
	entry->prev = entry;
	entry->next = entry;
}

#endif	/* __LIST_H__ */