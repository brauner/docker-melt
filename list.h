/*
 *
 * Copyright © 2016 Christian Brauner <christian.brauner@mailbox.org>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __LIST_H
#define __LIST_H

#include <stdio.h>

struct list {
	void *elem; // allow storage of generic data
	struct list *next;
	struct list *prev;
};

#define list_for_each(__it, __list)                                            \
	for (__it = (__list)->next; __it != __list; __it = __it->next)

// Mainly exists in order to safely free a circular linked list.
#define list_for_each_safe(__it, __list, __next)                               \
	for (__it = (__list)->next, __next = __it->next; __it != __list;       \
	     __it = __next, __next = __next->next)

static inline void list_init(struct list *list)
{
	list->elem = NULL;
	list->next = list->prev = list;
}

static inline void list_add_elem(struct list *list, void *elem)
{
	list->elem = elem;
}

static inline void list_add(struct list *new, struct list *prev,
			    struct list *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

static inline void list_add_tail(struct list *head, struct list *list)
{
	list_add(list, head->prev, head);
}

/* Return length of the list. */
static inline size_t list_len(struct list *list)
{
	size_t len = 0;
	struct list *it;
	list_for_each(it, list) { len++; }

	return len;
}

#endif // __LIST_H
