// This software is released as-is into the public domain, as described at
// https://unlicense.org. Do whatever you like with it.

// See https://github.com/glitchub/pace for more information.

char *usage = "\
Usage:\n\
\n\
    pace [options] [file]\n\
\n\
Write bytes from file or stdin to stdout at a specific pace, defined as\n\
nanoseconds per byte. The default pace is 86805 nS per byte to simulate 115,200\n\
baud N-8-1 UART transfer.\n\
\n\
Options:\n\
\n\
    -b baud     - simulate UART baud rate\n\
    -f          - also delay before the first byte\n\
    -n nS       - nanoseconds per byte, 1 to 999999999\n\
    -v          - report pace to stderr\n\
\n\
-b and -n are mutually exclusuive.\n\
";

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#define die(...) fprintf(stderr, __VA_ARGS__), exit(1)

#define QMAX 4096
unsigned char queue[QMAX];
int qhead, qcount;

pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;
#define qlock() pthread_mutex_lock(&qmutex)
#define qunlock() pthread_mutex_unlock(&qmutex)

int timerfd;
int status = 0;

// Thread to write queued characters to stdout per timerfd
// Exit on underflow or error with status set to reason
static void *dequeue(void * unused)
{
    while (!status)
    {
        // block on timerfd
        unsigned long long pending;
        if (read(timerfd, &pending, 8) != 8) { status = -errno; break; }

        qlock();
        while (pending--)
        {
            if (!qcount) { status = INT_MAX; break; }   // underflow
            if (write(1, queue + qhead, 1) != 1) { status = errno; break; }
            qcount--;
            qhead = (qhead + 1) % QMAX;
        }
        qunlock();
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    int nS = 87805; // nominal 115200 baud
    int verbose = 0;
    int first = 0;

    while(1) switch(getopt(argc, argv,":b:fn:v"))
    {
        case 'b':
        {
            int baud = (int)strtoul(optarg, NULL, 0);
            if (baud <= 10 || baud > 1000*1000) die("Invalid -b %s\n", optarg);
            nS = 10*((1000*1000*1000)/baud);
            break;
        }

        case 'f':
            first = 1;
            break;

        case 'n':
            nS = (int)strtoul(optarg, NULL, 0);
            if (nS <= 0 || nS >= 1000*1000*1000) die("Invalid -n %s\n", optarg);
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

    // pre-fill the queue
    qcount = read(input, &queue, QMAX);
    if (!qcount) return 0; // nothing to read
    if (qcount < 0) die("Input read failed: %s\n", strerror(errno));

    if (verbose) fprintf(stderr, "Pace is %dnS per byte\n", nS);

    // create timerfd for the desired pace
    timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timerfd < 0) die("Can't create timer: %s\n", strerror(errno));
    if (timerfd_settime(timerfd, 0, (struct itimerspec[]){{ .it_interval = {.tv_nsec = nS}, .it_value = {.tv_nsec = first ? nS : 1} }}, NULL))
        die("Can't set timer: %s\n", strerror(errno));

    // start the dequeue thread
    pthread_t thread;
    int e = pthread_create(&thread, NULL, &dequeue, NULL);
    if (e) die("Can't start dequeue thread: %s\n", strerror(e));

    while (1)
    {
        // get more input
        unsigned char buffer[4096];
        int got = read(input, buffer, sizeof(buffer));
        if (got < 0) die("Input read failed: %s\n", strerror(errno));

        if (!got)
        {
            pthread_join(thread, NULL);         // EOF, wait for dequeue thread to exit
            if (status == INT_MAX) status = 0;  // expect underflow
            goto out;
        }

        if (status) goto out;                   // done if thread exit

        // push buffer into queue as needed
        int i = 0;
        while (i < got)
        {
            if (qcount < QMAX)
            {
                qlock();
                queue[(qhead + qcount) % QMAX] = buffer[i++];
                qcount++;
                qunlock();
            }
            pthread_yield();
            if (status) goto out;
        }
    }

  out:
    if (status == INT_MAX) die("Dequeue underflow\n");
    if (status < 0) die("Dequeue timerfd read failed: %s\n", strerror(-status));
    if (status > 0) die("Dequeue stdout write failed: %s\n", strerror(status));
    return 0;
}
