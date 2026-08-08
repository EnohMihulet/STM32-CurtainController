#pragma once

#include <stddef.h>
#include <stdbool.h>

/*
	NOTE:
	#define CORE_QUEUE_IMPLEMENTATION
	before you include this file in only one C or C++ file

	NOTE:
	Safe only for plain old data / trivially copyable types.
	Not suitable for C++ types with constructors, destructors, or invariants.

QUEUE_DEFINE(PREFIX, TYPE, INIT_CAPACITY) generates:

typedef struct {
	size_t size, capacity, front, back;
	TYPE* items;
} PREFIX##_Queue;

bool PREFIX##_init(PREFIX##_Queue* q);
void PREFIX##_free(PREFIX##_Queue* q);
bool PREFIX##_is_full(const PREFIX##_Queue* q);
bool PREFIX##_is_empty(const PREFIX##_Queue* q);
void PREFIX##_clear(PREFIX##_Queue* q);
bool PREFIX##_enqueue(PREFIX##_Queue* q, TYPE item);
bool PREFIX##_dequeue(PREFIX##_Queue* q, TYPE* out);
bool PREFIX##_front(const PREFIX##_Queue* q, TYPE* out);
*/

#define CORE_QUEUE_DEFAULT_CAPACITY 8

#ifdef CORE_QUEUE_IMPLEMENTATION
	#define CORE_QUEUE_IMPL(...) __VA_ARGS__
#else
	#define CORE_QUEUE_IMPL(...)
#endif

#define QUEUE_DEFINE(PREFIX, TYPE, INIT_CAPACITY)                                                          \
                                                                                                           \
	typedef struct {                                                                                   \
		size_t size, capacity, front, back;                                                        \
		TYPE items[(INIT_CAPACITY) == 0 ? CORE_QUEUE_DEFAULT_CAPACITY : (INIT_CAPACITY)];           \
	} PREFIX##_Queue;                                                                                  \
                                                                                                           \
	bool PREFIX##_init(PREFIX##_Queue* q);                                                             \
	void PREFIX##_free(PREFIX##_Queue* q);                                                             \
	bool PREFIX##_is_full(const PREFIX##_Queue* q);                                                    \
	bool PREFIX##_is_empty(const PREFIX##_Queue* q);                                                   \
	void PREFIX##_clear(PREFIX##_Queue* q);                                                            \
	bool PREFIX##_enqueue(PREFIX##_Queue* q, TYPE item);                                               \
	bool PREFIX##_dequeue(PREFIX##_Queue* q, TYPE* out);                                               \
	bool PREFIX##_front(const PREFIX##_Queue* q, TYPE* out);                                           \
                                                                                                           \
	CORE_QUEUE_IMPL(                                                                                   \
		static size_t PREFIX##_inc_impl(const PREFIX##_Queue* q, size_t i) {                       \
			return (i + 1 == q->capacity) ? 0 : i + 1;                                         \
		}                                                                                          \
                                                                                                           \
		bool PREFIX##_init(PREFIX##_Queue* q) {                                                    \
			if (q == NULL) return false;                                                       \
			size_t cap = (INIT_CAPACITY) == 0 ? CORE_QUEUE_DEFAULT_CAPACITY : (INIT_CAPACITY); \
			q->front = 0;                                                                      \
			q->back = 0;                                                                       \
			q->size = 0;                                                                       \
			q->capacity = cap;                                                                 \
			return true;                                                                       \
		}                                                                                          \
                                                                                                            \
		void PREFIX##_free(PREFIX##_Queue* q) {                                                    \
			if (q == NULL) return;                                                            \
			q->front = 0;                                                                      \
			q->back = 0;                                                                       \
			q->size = 0;                                                                       \
			q->capacity = 0;                                                                   \
		}                                                                                          \
                                                                                                            \
		bool PREFIX##_is_full(const PREFIX##_Queue* q) {                                           \
			if (q == NULL) return false;                                                      \
			return q->size >= q->capacity;                                                     \
		}                                                                                          \
                                                                                                            \
		bool PREFIX##_is_empty(const PREFIX##_Queue* q) {                                          \
			if (q == NULL) return true;                                                       \
			return q->size == 0;                                                               \
		}                                                                                          \
                                                                                                            \
		void PREFIX##_clear(PREFIX##_Queue* q) {                                                   \
			if (q == NULL) return;                                                            \
			q->front = 0;                                                                      \
			q->back = 0;                                                                       \
			q->size = 0;                                                                       \
		}                                                                                          \
                                                                                                            \
		bool PREFIX##_enqueue(PREFIX##_Queue* q, TYPE item) {                                      \
			if (q == NULL || PREFIX##_is_full(q)) return false;                                \
			if (PREFIX##_is_empty(q)) {                                                       \
				q->front = 0;                                                              \
				q->back = 0;                                                               \
			} else {                                                                           \
				q->back = PREFIX##_inc_impl(q, q->back);                                  \
			}                                                                                  \
			q->items[q->back] = item;                                                          \
			q->size++;                                                                         \
			return true;                                                                       \
		}                                                                                          \
                                                                                                            \
		bool PREFIX##_dequeue(PREFIX##_Queue* q, TYPE* out) {                                      \
			if (q == NULL || out == NULL || PREFIX##_is_empty(q)) return false;                \
			*out = q->items[q->front];                                                         \
			q->size--;                                                                         \
			if (PREFIX##_is_empty(q)) {                                                        \
				q->front = 0;                                                              \
				q->back = 0;                                                               \
			} else {                                                                           \
				q->front = PREFIX##_inc_impl(q, q->front);                                \
			}                                                                                  \
			return true;                                                                       \
		}                                                                                          \
                                                                                                            \
		bool PREFIX##_front(const PREFIX##_Queue* q, TYPE* out) {                                  \
			if (q == NULL || out == NULL || PREFIX##_is_empty(q)) return false;                \
			*out = q->items[q->front];                                                         \
			return true;                                                                       \
		}                                                                                          \
	)
