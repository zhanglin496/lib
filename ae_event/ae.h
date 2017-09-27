/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __AE_H__
#define __AE_H__

#include <time.h>
#include <unistd.h>

#define AE_OK	0
#define AE_ERR	-1

#define AE_NONE	0
#define AE_READABLE	1
#define AE_WRITABLE	2

#define AE_FILE_EVENTS	1
#define AE_TIME_EVENTS	2
#define AE_ALL_EVENTS	(AE_FILE_EVENTS|AE_TIME_EVENTS)
#define AE_DONT_WAIT	4

#define AE_NOMORE	-1

/* Macros */
#define AE_NOTUSED(V) ((void) V)

struct aeEventLoop;

ssize_t tread(int fd, void *buf, size_t nbytes, unsigned int timout);
ssize_t treadn(int fd, void *buf, size_t nbytes, unsigned int timout);
ssize_t writen(int fd, const void *buf, size_t n);
int setnonblock(int sfd);

#define EV_DOWN_CAST(X) ((aeEventLoop *)X)

/* Types and data structures */
typedef int aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData, int mask);
typedef int aeTimeProc(struct aeEventLoop *eventLoop, void *clientData);
typedef void aeBeforeSleepProc(struct aeEventLoop *eventLoop);

/* File event structure */
typedef struct aeFileEvent {
    int mask; /* one of AE_(READABLE|WRITABLE) */
    aeFileProc *rfileProc;
    aeFileProc *wfileProc;
    void *clientData;
} aeFileEvent;

/* Time event structure */
typedef struct aeTimeEvent {
    int min_heap_idx;
    long when_sec; /* seconds */
    long when_ms; /* milliseconds */
    aeTimeProc *timeProc;
    void *clientData;
} aeTimeEvent;

/* A fired event */
typedef struct aeFiredEvent {
    int fd;
    int mask;
} aeFiredEvent;


typedef struct min_heap {
        aeTimeEvent **p; //pointer array for record timer event
        unsigned int n; //current use num of heap
        unsigned int a; //all num of heap
} min_heap_t;

/* State of an event based program */
typedef struct aeEventLoop {
    int maxfd;   /* highest file descriptor currently registered */
    int setsize; /* max number of file descriptors tracked */
    long long timeEventNextId;
    time_t lastTime;     /* Used to detect system clock skew */
    aeFileEvent *events; /* Registered events */
    aeFiredEvent *fired; /* Fired events */
    struct min_heap heap;
    int stop;
    void *apidata; /* This is used for polling API specific data */
    aeBeforeSleepProc *beforesleep;
} aeEventLoop;


#define for_each_min_heap_timer(te, heap, n) \
        int __tmp_n; \
        for (__tmp_n = 0; __tmp_n < n && (te = (heap)->p[__tmp_n]); __tmp_n++)

/* Prototypes */
aeEventLoop *aeCreateEventLoop(int setsize);
void aeDeleteEventLoop(aeEventLoop *eventLoop);
void aeStop(aeEventLoop *eventLoop);
int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
        aeFileProc *proc, void *clientData);
void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask);
int aeGetFileEvents(aeEventLoop *eventLoop, int fd);
int aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
		aeTimeEvent *te, aeTimeProc *proc, void *clientData);
int aeDeleteTimeEvent(aeEventLoop *eventLoop, aeTimeEvent *te);
int aeModifyTimeEvent(aeEventLoop *eventLoop, long long milliseconds, aeTimeEvent *te);
int aeProcessEvents(aeEventLoop *eventLoop, int flags);
int aeWait(int fd, int mask, long long milliseconds);
void aeMain(aeEventLoop *eventLoop);
char *aeGetApiName(void);
void aeSetBeforeSleepProc(aeEventLoop *eventLoop, aeBeforeSleepProc *beforesleep);
int aeGetSetSize(aeEventLoop *eventLoop);
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize);

int min_heap_elt_is_top(const aeTimeEvent *e);
int min_heap_empty(min_heap_t *s);
unsigned int min_heap_size(min_heap_t *s);
aeTimeEvent *min_heap_top(min_heap_t *s);
aeTimeEvent *min_heap_pop(min_heap_t *s);
int min_heap_erase(min_heap_t *s, aeTimeEvent *e);
min_heap_t *min_heap_init(min_heap_t *heap);
void min_heap_dtor(min_heap_t *s);
int aetimer_event_add(min_heap_t *s, aeTimeEvent *te);

#endif
