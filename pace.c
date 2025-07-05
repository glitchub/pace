// This software is released as-is into the public domain, as described at
// https://unlicense.org. Do whatever you like with it.

// See https://github.com/glitchub/pace for more information.

char *usage = "\
Usage:\n\
\n\
    pace [options] [file]\n\
\n\
Write bytes from file or stdin to stdout at a specific pace, defined as\n\
nanoseconds per byte. The default pace is 86805nS per byte to simulate 115,200\n\
baud N-8-1 UART transfer.\n\
\n\
Options:\n\
\n\
    -b baud     - simulate UART baud rate\n\
    -f          - also delay before the first byte\n\
    -n nS       - nanoseconds per byte, 100 to 999999999\n\
    -s bytes    - size of input buffer buffer, default 65536\n\
    -v          - report the pace on stderr\n\
\n\
-b and -n are mutually exclusive. Exits immediately after last byte is sent.\n\
";

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>
#include "queue.h"

#define die(...) fprintf(stderr, __VA_ARGS__), exit(1)

int timerfd;              // timerfd handle for dequeue thread

queue q;                  // queued data

// lock/unlock queue access
pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;
#define qlock() pthread_mutex_lock(&qmutex)
#define qunlock() pthread_mutex_unlock(&qmutex)

// Return true if file descriptor can be read
int readable(int fd)
{
    struct pollfd p = { .fd = fd, .events = POLLIN };
    return poll(&p, 1, 0);
}

// Thread to write queued characters to stdout per timerfd
// Exit with status == INT_MAX on underflow, < 0 for timer error, > 0 for write error
volatile int status = 0;
static void *dequeue(void * unused)
{
    unsigned long long pending = 0;
    void *data;
    while (1)
    {
        sched_yield();
        if (!pending)
        {
            if (!readable(timerfd)) continue;
            if (read(timerfd, &pending, 8) != 8) { status = -errno; break; }
        }
        if (pending)
        {
            qlock();
            unsigned int ready = getq(&q, &data);
            if (!ready) { status = INT_MAX; break; }
            if (ready > pending) ready = pending;
            if (write(1, data, ready) != ready) { status = errno; break; }
            delq(&q, ready);
            qunlock();
            pending -= ready;
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    int nS = 86805; // nS per byte, nominal 115200 baud
    int verbose = 0, first = 0, size = 65536;

    while(1) switch(getopt(argc, argv,":b:fn:s:v"))
    {
        case 'b':
        {
            int baud = (int)strtoul(optarg, NULL, 0);
            if (baud <= 10 || baud > 1000*1000) die("Invalid -b %s\n", optarg);
            nS = (10*1000*1000*1000LL)/baud;
            break;
        }

        case 'f':
            first = 1;
            break;

        case 'n':
            nS = (int)strtoul(optarg, NULL, 0);
            if (nS < 100 || nS >= 1000*1000*1000) die("Invalid -n %s\n", optarg);
            break;

        case 's':
            size = (int)strtoul(optarg, NULL, 0);
            if (size < 1) die("Invalid -s %s\n", optarg);
            break;

        case 'v':
            verbose = 1;
            break;

        case ':':
        case '?': die(usage);
        case -1: goto optx;

    } optx:;

    int input = 0; // default stdin
    if (optind < argc)
    {
        input = open(argv[optind], O_RDONLY);
        if (input < 0) die("Can't open %s: %s\n", argv[optind], strerror(errno));
    }

    if (!initq(&q, size)) die("initq failed\n");

    unsigned char buffer[4096];
    int got = read(input, buffer, sizeof(buffer));
    if (got < 0) die ("Input read failed: %s\n", strerror(errno));
    if (!got) return 0; // nothing to read
    if (!putq(&q, buffer, got)) die("putq failed!\n");

    if (verbose) fprintf(stderr, "Pace is %dnS per byte\n", nS);

    // create timerfd for the desired pace
    timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timerfd < 0) die("Can't create timer: %s\n", strerror(errno));
    // start the dequeue thread
    pthread_t thread;
    int e = pthread_create(&thread, NULL, &dequeue, NULL);
    if (e) die("Can't start dequeue thread: %s\n", strerror(e));

    // start the timer
    if (timerfd_settime(timerfd, 0, (struct itimerspec[]){{ .it_interval = {.tv_nsec = nS}, .it_value = {.tv_nsec = first ? nS : 1} }}, NULL))
        die("Can't set timer: %s\n", strerror(errno));

    got = 0;
    while (1)
    {
        sched_yield();
        if (status) break;

        if (!got)
        {
            if (!readable(input)) continue;
            got = read(input, buffer, sizeof(buffer));
            if (got < 0) die("Input read failed: %s\n", strerror(errno));
            if (!got)
            {
                pthread_join(thread, NULL);         // EOF, wait for dequeue thread to underflow
                if (status == INT_MAX) status = 0;  // expect underflow
                break;
            }
        }

        if (got)
        {
            qlock();
            if (putq(&q, buffer, got)) got = 0;
            qunlock();
        }
    }

    if (status == INT_MAX) die("Dequeue underflow\n");
    if (status < 0) die("Dequeue timerfd read failed: %s\n", strerror(-status));
    if (status > 0) die("Dequeue stdout write failed: %s\n", strerror(status));
    return 0;
}
