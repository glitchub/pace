// Byte queue primitives

typedef struct
{
    unsigned int oldest,    // index of oldest queued byte
                 count,     // number of queued bytes
                 size;      // size of data buffer
    unsigned char *data;    // malloced data buffer
} queue;

bool initq(queue *q, unsigned int size);
bool putq(queue *q, void *bytes, unsigned int count);
unsigned int getq(queue *q, void **bytes);
void delq(queue *q, unsigned int count);
void freeq(queue *q);

// Wipe the queue
#define wipeq(q) delq((q),(q)->count)

// Return number of bytes in queue
#define availq(q) ((q)->count)
