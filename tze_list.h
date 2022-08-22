#ifndef TZE_LIST_H
#define TZE_LIST_H

#include <stdbool.h>

struct tze_list_t {
	struct tze_list_t *next;
	struct tze_list_t *prev;
};

#define TZE_LIST_HEAD(name)											\
	struct tze_list_t name = {&(name), &(name)}

static inline void tze_list_init(struct tze_list_t *entry)
{
	entry->next = entry->prev = entry;
}

static inline void tze_list_add(struct tze_list_t *new_entry,
								struct tze_list_t *prev,
								struct tze_list_t *next)
{
	next->prev = new_entry;
	new_entry->next = next;
	new_entry->prev = prev;
	prev->next = new_entry;
}

static inline void tze_list_add_tail(struct tze_list_t *head,
									 struct tze_list_t *new_entry)
{
	tze_list_add(new_entry, head->prev, head);
}

static inline void tze_list_del(struct tze_list_t *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
	tze_list_init(entry);
}

static inline bool tze_list_is_empty(const struct tze_list_t *head)
{
	return (head->next == head) ? true : false;
}

#define tze_list_entry(ptr, type, member)							\
	((type *) (((char *) ptr) - ((char *) &((type *) 0)->member)))

#define tze_list_foreach_entry(e, type, member, head)				\
	for (e = tze_list_entry((head)->next, type, member);			\
		 &e->member != (head);										\
	     e = tze_list_entry(e->member.next, type, member))

#endif	/* TZE_LIST_H */
