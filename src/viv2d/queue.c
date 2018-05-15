/*

Copyright (c) 2005-2008, Simon Howard

Permission to use, copy, modify, and/or distribute this software
for any purpose with or without fee is hereby granted, provided
that the above copyright notice and this permission notice appear
in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

 */

#include <stdint.h>
#include <stdlib.h>
#include "queue.h"

/* A double-ended queue */

typedef struct _queue_entry_t queue_entry_t;

struct _queue_entry_t {
	queue_value_t data;
	queue_entry_t *prev;
	queue_entry_t *next;
};

struct _queue_t {
	queue_entry_t *head;
	queue_entry_t *tail;
	uint32_t size;
};

queue_t *queue_new(void)
{
	queue_t *queue;

	queue = (queue_t *) malloc(sizeof(queue_t));

	if (queue == NULL) {
		return NULL;
	}

	queue->head = NULL;
	queue->tail = NULL;
	queue->size = 0;

	return queue;
}

void queue_free(queue_t *queue)
{
	while (!queue_is_empty(queue)) {
		queue_pop_head(queue);
	}

	free(queue);
}

int queue_push_head(queue_t *queue, queue_value_t data)
{
	queue_entry_t *new_entry;

	new_entry = malloc(sizeof(queue_entry_t));

	if (new_entry == NULL) {
		return 0;
	}

	new_entry->data = data;
	new_entry->prev = NULL;
	new_entry->next = queue->head;

	if (queue->head == NULL) {
		queue->head = new_entry;
		queue->tail = new_entry;
	} else {
		queue->head->prev = new_entry;
		queue->head = new_entry;
	}

	queue->size++;

	return 1;
}

queue_value_t queue_pop_head(queue_t *queue)
{
	queue_entry_t *entry;
	queue_value_t result;

	if (queue_is_empty(queue)) {
		return QUEUE_NULL;
	}

	entry = queue->head;
	queue->head = entry->next;
	result = entry->data;

	if (queue->head == NULL) {
		queue->tail = NULL;
	} else {
		queue->head->prev = NULL;
	}

	free(entry);

	queue->size--;

	return result;
}

queue_value_t queue_peek_head(queue_t *queue)
{
	if (queue_is_empty(queue)) {
		return QUEUE_NULL;
	} else {
		return queue->head->data;
	}
}

int queue_push_tail(queue_t *queue, queue_value_t data)
{
	queue_entry_t *new_entry;

	new_entry = malloc(sizeof(queue_entry_t));

	if (new_entry == NULL) {
		return 0;
	}

	new_entry->data = data;
	new_entry->prev = queue->tail;
	new_entry->next = NULL;

	if (queue->tail == NULL) {
		queue->head = new_entry;
		queue->tail = new_entry;
	} else {
		queue->tail->next = new_entry;
		queue->tail = new_entry;
	}

	queue->size++;

	return 1;
}

queue_value_t queue_pop_tail(queue_t *queue)
{
	queue_entry_t *entry;
	queue_value_t result;

	if (queue_is_empty(queue)) {
		return QUEUE_NULL;
	}

	entry = queue->tail;
	queue->tail = entry->prev;
	result = entry->data;

	if (queue->tail == NULL) {
		queue->head = NULL;
	} else {
		queue->tail->next = NULL;
	}

	free(entry);

	queue->size--;

	return result;
}

queue_value_t queue_peek_tail(queue_t *queue)
{
	if (queue_is_empty(queue)) {
		return QUEUE_NULL;
	} else {
		return queue->tail->data;
	}
}

int queue_is_empty(queue_t *queue)
{
	return queue->head == NULL;
}


uint32_t queue_size(queue_t *queue)
{
	return queue->size;
}
