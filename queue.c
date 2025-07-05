// Byte-queue primitives

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include "queue.h"

// Initialize a queue to specified size and return true, or false if not enough memory
bool initq(queue *q, unsigned int size)
{
    q->data = malloc(size);
    if (!q->data) return false;
    q->size = size;
    q->oldest = 0;
    q->count = 0;
    return true;
}

// Add count bytes of data to specified queue and return true, or false of not enough free space to
// contain the entire thing. Will always return false if count exceeds q->size!
bool putq(queue *q, void *data, unsigned int count)
{
    if (count > q->size - q->count) return false;
    for (unsigned int i = 0; i < count; i++, q->count++)
        q->data[(q->oldest + q->count) % q->size] = *(unsigned char *)(data + i);
    return true;
}

// Point *data at monolithic chunk of queued data and return its length, or 0 if queue is empty.
// Note, will return less than q->count on wrap around. Caller must call delq() once the returned
// data has been consumed.
unsigned int getq(queue *q, void **data)
{
    if (!q->count) return 0;
    *data = q->data + q->oldest;
    unsigned int len = q->size - q->oldest;
    if (len > q->count) len = q->count;
    return len;
}

// Remove specified number of characters from queue, after using getq.
// If count >= q->count, delete everything.
void delq(queue *q, unsigned int count)
{
    if (!count) return;
    if (count >= q->count)
    {
        q->count = 0;
        q->oldest = 0;
        return;
    }
    q->count -= count;
    q->oldest = (q->oldest + count) % q->size;
}

// Free malloc'd queue
void freeq(queue *q)
{
    free(q->data);  // OK if NULL
    q->data = NULL;
}
