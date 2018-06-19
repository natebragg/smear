#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "queue.h"

#define ERROR_MSG(msg) fprintf(stderr, "%s - %s\n", __FUNCTION__, msg)

typedef void (*handler_t)(const void *);

typedef struct
{
    const void *wrapper;
    handler_t handler;
} mq_msg_t;

static queue_t *q;
static pthread_t tid;
static bool queue_empty;

/* The plan:
 *
 * Put messages received onto a queue inside the _Send_Message
 * function; pop messages off the queue and send them on using the
 * _Handle_Message function. Messages sent come with pointers to
 * wrappers (cast to void) and pointers to the appropriate
 * _Handle_Message function. This can all just use the queue
 * implementation from queue.c.
 */

static void flushEventQueue(void)
{
    bool success;
    mq_msg_t *qmsg;

    while(size(q) > 0)
    {
        success = dequeue(q, (const void **)&qmsg);
        if (!success)
        {
            ERROR_MSG("Failed to dequeue element.");
            exit(-1);
        }
        qmsg->handler(qmsg->wrapper);
        free(qmsg);
    }
    queue_empty = true;
}

static void *mainloop(void *unused)
{
    while (true)
    {
        flushEventQueue();
        sleep(0);
    }
    return NULL;
}

void SMUDGE_debug_print(const char *fmt, const char *a1, const char *a2)
{
    fprintf(stderr, fmt, a1, a2);
}

void SMUDGE_free(const void *a1)
{
    free((void *)a1);
}

void SMUDGE_panic(void)
{
    exit(-1);
}

void SMUDGE_panic_print(const char *fmt, const char *a1, const char *a2)
{
    SMUDGE_debug_print(fmt, a1, a2);
    SMUDGE_panic();
}

void SRT_send_message(const void *msg, void (handler)(const void *))
{
    mq_msg_t *qmsg;

    if (msg == NULL)
    {
        ERROR_MSG("Null message sent.");
        exit(-3);
    }
    qmsg = malloc(sizeof(mq_msg_t));
    if (qmsg == NULL)
    {
        ERROR_MSG("Failed to allocate wrapper memory.");
        exit(-2);
    }
    qmsg->wrapper = msg;
    qmsg->handler = handler;
    // There's a race here: set to false, then the main loop flushes
    // the queue and sets it to true. Then we enqueue something, and
    // the queue is not empty but queue_empty is true. Then someone
    // calls wait_for_idle, which exits immediately even though
    // there's something in the queue. This can only happen if the
    // event was sent by a thread other than the one calling
    // wait_for_idle, which I'm ok with.

    // Setting the flag after enqueue would lead to a different race,
    // where the thread that sends the event could falsely never
    // return from wait_for_idle, because the event was dequeued
    // between the enqueue call and the flag getting set. That would
    // be worse.

    // Better would be always having the correct value in queue_empty,
    // but that would require locking the queue.
    queue_empty = false;
    if (!enqueue(q, qmsg))
    {
        ERROR_MSG("Failed to enqueue message.");
        exit(-2);
    }
}

void SRT_init(void)
{
    q = newq();
}

void SRT_run(void)
{
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_create(&tid, &attr, mainloop, NULL);
    pthread_attr_destroy(&attr);
}

static void SRT_join(void)
{
    void *rv;
    pthread_join(tid, &rv);
}

void SRT_wait_for_idle(void)
{
    return wait_empty(q);
}

void SRT_stop(void)
{
    void *rv;
    pthread_cancel(tid);
    pthread_join(tid, &rv);
    freeq(q);
}
