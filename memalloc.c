/*
 * memalloc.c
 * Implements malloc(), free(), calloc(), and realloc().
 *
 * Build as a shared library on Linux:
 *   gcc -Wall -Wextra -Wpedantic -fPIC -shared memalloc.c -o memalloc.so -pthread
 *
 */

#define _DEFAULT_SOURCE

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

/*
 * max_align_t gives the header an alignment suitable for ordinary C object
 * types. The union's size is rounded up to satisfy its alignment requirement.
 */
typedef union header header_t;

union header {
    struct {
        size_t size;
        int is_free;
        header_t *next;
    } data;
    max_align_t alignment_member;
};

static header_t *head = NULL;
static header_t *tail = NULL;
static pthread_mutex_t allocator_lock = PTHREAD_MUTEX_INITIALIZER;

/* Find the first free block that is large enough. */
static header_t *find_free_block(size_t size)
{
    header_t *current = head;

    while (current != NULL) {
        if (current->data.is_free && current->data.size >= size) {
            return current;
        }
        current = current->data.next;
    }

    return NULL;
}

/*
 * Request more memory from the process break.
 * Caller must hold allocator_lock.
 */
static header_t *request_space(size_t size)
{
    void *memory;
    header_t *header;

    /* Prevent size arithmetic from wrapping around. */
    if (size > SIZE_MAX - sizeof(header_t) ||
        size > (size_t)INTPTR_MAX - sizeof(header_t)) {
        return NULL;
    }

    memory = sbrk((intptr_t)(sizeof(header_t) + size));
    if (memory == (void *)-1) {
        return NULL;
    }

    header = (header_t *)memory;
    header->data.size = size;
    header->data.is_free = 0;
    header->data.next = NULL;

    if (head == NULL) {
        head = header;
    }

    if (tail != NULL) {
        tail->data.next = header;
    }
    tail = header;

    return header;
}

void *malloc(size_t size)
{
    header_t *header;
    void *result;

    if (size == 0) {
        return NULL;
    }

    if (pthread_mutex_lock(&allocator_lock) != 0) {
        return NULL;
    }

    header = find_free_block(size);
    if (header != NULL) {
        header->data.is_free = 0;
    } else {
        header = request_space(size);
    }

    result = (header == NULL) ? NULL : (void *)(header + 1);

    (void)pthread_mutex_unlock(&allocator_lock);
    return result;
}

void free(void *ptr)
{
    header_t *header;
    header_t *current;
    header_t *previous;
    void *program_break;

    if (ptr == NULL) {
        return;
    }

    if (pthread_mutex_lock(&allocator_lock) != 0) {
        return;
    }

    /*
     * Only accept pointers that match the start of a block returned by this
     * allocator. Passing an unrelated pointer to free() is still invalid C.
     */
    header = (header_t *)ptr - 1;
    current = head;
    previous = NULL;

    while (current != NULL && current != header) {
        previous = current;
        current = current->data.next;
    }

    if (current == NULL || current->data.is_free) {
        (void)pthread_mutex_unlock(&allocator_lock);
        return;
    }

    current->data.is_free = 1;

    /*
     * If the freed block is the last block at the program break, attempt to
     * return it to the OS. Otherwise, leave it available for later reuse.
     */
    program_break = sbrk(0);
    if (current == tail &&
        program_break == (void *)((char *)current +
                                  sizeof(header_t) + current->data.size)) {
        intptr_t amount = (intptr_t)(sizeof(header_t) + current->data.size);

        if (sbrk(-amount) != (void *)-1) {
            if (previous == NULL) {
                head = NULL;
                tail = NULL;
            } else {
                previous->data.next = NULL;
                tail = previous;
            }
        }
    }

    (void)pthread_mutex_unlock(&allocator_lock);
}

void *calloc(size_t count, size_t size)
{
    size_t total;
    void *ptr;

    if (count == 0 || size == 0) {
        return NULL;
    }

    if (size > SIZE_MAX / count) {
        return NULL;
    }

    total = count * size;
    ptr = malloc(total);

    if (ptr != NULL) {
        memset(ptr, 0, total);
    }

    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    header_t *old_header;
    void *new_ptr;
    size_t old_size;

    if (ptr == NULL) {
        return malloc(size);
    }

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    if (pthread_mutex_lock(&allocator_lock) != 0) {
        return NULL;
    }

    old_header = (header_t *)ptr - 1;

    /*
     * Confirm that ptr belongs to this allocator before reading its metadata.
     */
    {
        header_t *current = head;
        while (current != NULL && current != old_header) {
            current = current->data.next;
        }

        if (current == NULL || current->data.is_free) {
            (void)pthread_mutex_unlock(&allocator_lock);
            return NULL;
        }
    }

    old_size = old_header->data.size;

    if (old_size >= size) {
        (void)pthread_mutex_unlock(&allocator_lock);
        return ptr;
    }

    (void)pthread_mutex_unlock(&allocator_lock);

    new_ptr = malloc(size);
    if (new_ptr == NULL) {
        return NULL; /* Original allocation remains valid. */
    }

    memcpy(new_ptr, ptr, old_size);
    free(ptr);

    return new_ptr;
}
