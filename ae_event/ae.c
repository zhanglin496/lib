/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/sysinfo.h>


/* Include the best multiplexing layer supported by this system.
 * The following should be ordered by performances, descending. */


#define zmalloc	malloc
#define	zfree	free
#define	zrealloc realloc

#include "ae.h"

#ifdef HAVE_EPOLL
#	include "ae_epoll.h"
#else
#	include "ae_select.h"
#endif

aeEventLoop *aeCreateEventLoop(int setsize)
{
	aeEventLoop *eventLoop;
	int i;

	if (!(eventLoop = zmalloc(sizeof(*eventLoop))))
		goto err;

	eventLoop->events = zmalloc(sizeof(aeFileEvent) * setsize);

	#ifdef HAVE_EPOLL	
	eventLoop->fired = zmalloc(sizeof(aeFiredEvent) * MAX_FIRED_EVENTS);
	#else
	eventLoop->fired = zmalloc(sizeof(aeFiredEvent) * setsize);
	#endif

	if (!eventLoop->events || !eventLoop->fired)
		goto err;

	eventLoop->setsize = setsize;
	eventLoop->lastTime = time(NULL);
	min_heap_init(&eventLoop->heap);
	eventLoop->stop = 0;
	eventLoop->maxfd = -1;
	eventLoop->beforesleep = NULL;
	if (aeApiCreate(eventLoop) == -1)
		goto err;

    /* Events with mask == AE_NONE are not set. So let's initialize the
     * vector with it. */
	for (i = 0; i < setsize; i++)
		eventLoop->events[i].mask = AE_NONE;

	return eventLoop;
err:
	if (eventLoop) {
		zfree(eventLoop->events);
		zfree(eventLoop->fired);
		zfree(eventLoop);
	}
	return NULL;
}

/* Return the current set size. */
int aeGetSetSize(aeEventLoop *eventLoop)
{
	return eventLoop->setsize;
}

/* Resize the maximum set size of the event loop.
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * Otherwise AE_OK is returned and the operation is successful. */
int aeResizeSetSize(aeEventLoop *eventLoop, int setsize)
{
	int i;
	void *events;

	if (setsize == eventLoop->setsize)
		return AE_OK;
	if (eventLoop->maxfd >= setsize)
		return AE_ERR;

	#ifndef HAVE_EPOLL
	if (aeApiResize(eventLoop, setsize) == -1)
		return AE_ERR;
	#endif

	events = zrealloc(eventLoop->events, sizeof(aeFileEvent) * setsize);
	if (!events)
		return AE_ERR;

	eventLoop->events = events;
	eventLoop->setsize = setsize;

    /* Make sure that if we created new slots, they are initialized with
     * an AE_NONE mask. */
	for (i = eventLoop->maxfd + 1; i < setsize; i++)
		eventLoop->events[i].mask = AE_NONE;
	return AE_OK;
}

static void aeDeleteMinheap(aeEventLoop *eventLoop)
{
	min_heap_dtor(&eventLoop->heap);
}

void aeDeleteEventLoop(aeEventLoop *eventLoop)
{
	if (!eventLoop)
		return;

	aeApiFree(eventLoop);
	zfree(eventLoop->events);
	zfree(eventLoop->fired);
	aeDeleteMinheap(eventLoop);
	zfree(eventLoop);
}

void aeStop(aeEventLoop *eventLoop)
{
	eventLoop->stop = 1;
}

int aeCreateFileEvent(aeEventLoop *eventLoop, int fd, int mask,
		      aeFileProc *proc, void *clientData)
{
	if (fd < 0)
		return AE_ERR;

	if (fd >= eventLoop->setsize) {
		int setsize = eventLoop->setsize;

		do {
			setsize <<= 1;
		} while (fd >= setsize);

		if (aeResizeSetSize(eventLoop, setsize) != AE_OK)
			return AE_ERR;

		eventLoop->setsize = setsize;
	}

	aeFileEvent *fe = &eventLoop->events[fd];

	if (aeApiAddEvent(eventLoop, fd, mask) == -1)
		return AE_ERR;

	fe->mask |= mask;
	if (mask & AE_READABLE)
		fe->rfileProc = proc;
	if (mask & AE_WRITABLE)
		fe->wfileProc = proc;
	fe->clientData = clientData;

	if (fd > eventLoop->maxfd)
		eventLoop->maxfd = fd;

	return AE_OK;
}

void aeDeleteFileEvent(aeEventLoop *eventLoop, int fd, int mask)
{
	if (fd >= eventLoop->setsize)
		return;
	aeFileEvent *fe = &eventLoop->events[fd];

	if (fe->mask == AE_NONE)
		return;

	fe->mask = fe->mask & (~mask);
	if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
		/* Update the max fd */
		int j;

		for (j = eventLoop->maxfd - 1; j >= 0; j--)
			if (eventLoop->events[j].mask != AE_NONE)
				break;
		eventLoop->maxfd = j;
	}

	aeApiDelEvent(eventLoop, fd, mask);
}

int aeGetFileEvents(aeEventLoop *eventLoop, int fd)
{
	if (fd >= eventLoop->setsize)
		return 0;
	aeFileEvent *fe = &eventLoop->events[fd];

	return fe->mask;
}

static void aeGetTime(long *seconds, long *milliseconds)
{
#if 0
	struct timeval tv;

	gettimeofday(&tv, NULL);
	*seconds = tv.tv_sec;
	*milliseconds = tv.tv_usec / 1000;
#endif
	struct sysinfo info;
	sysinfo(&info);
	*seconds = info.uptime;
	*milliseconds = 0L;
}

static void aeAddMillisecondsToNow(long long milliseconds, long *sec,
				   long *ms)
{
	long cur_sec, cur_ms, when_sec, when_ms;

	aeGetTime(&cur_sec, &cur_ms);
	when_sec = cur_sec + milliseconds / 1000;
	when_ms = cur_ms + milliseconds % 1000;
	if (when_ms >= 1000) {
		when_sec++;
		when_ms -= 1000;
	}
	*sec = when_sec;
	*ms = when_ms;
}

int aeCreateTimeEvent(aeEventLoop *eventLoop,
			    long long milliseconds, aeTimeEvent *te,
			    aeTimeProc *proc, void *clientData)
{
	if (!te)
		return AE_ERR;

	aeAddMillisecondsToNow(milliseconds, &te->when_sec, &te->when_ms);
	te->timeProc = proc;
	te->clientData = clientData;

	/* add timer to min heap */
	if (aetimer_event_add(&eventLoop->heap, te) < 0)
		return AE_ERR;

	return AE_OK;
}

int aeDeleteTimeEvent(aeEventLoop *eventLoop, aeTimeEvent *te)
{
	if (te->min_heap_idx == -1)
		return AE_ERR;

	min_heap_erase(&eventLoop->heap, te);
	return AE_OK;
}

int aeModifyTimeEvent(aeEventLoop *eventLoop, long long milliseconds, aeTimeEvent *te)
{
	min_heap_erase(&eventLoop->heap, te);
	aeAddMillisecondsToNow(milliseconds, &te->when_sec, &te->when_ms);
	return aetimer_event_add(&eventLoop->heap, te);
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */

/* Process time events */
static int processTimeEvents(aeEventLoop *eventLoop)
{
	int processed = 0;
	aeTimeEvent *te;
	time_t now = time(NULL);

    /* If the system clock is moved to the future, and then set back to the
     * right value, time events may be delayed in a random way. Often this
     * means that scheduled operations will not be performed soon enough.
     *
     * Here we try to detect system clock skews, and force all the time
     * events to be processed ASAP when this happens: the idea is that
     * processing events earlier is less dangerous than delaying them
     * indefinitely, and practice suggests it is. */
	#if 0
	if (now < eventLoop->lastTime) {
		for_each_min_heap_timer(te, &eventLoop->heap, min_heap_size(&eventLoop->heap)) {
			/* trigger current all timer && sure new timer is the last one */
			te->when_sec = 0;
			te->when_ms = 0;
		}
	} 
	#endif
	eventLoop->lastTime = now;

	while ((te = min_heap_top(&eventLoop->heap))) {
		long now_sec, now_ms;

		aeGetTime(&now_sec, &now_ms);
		if (now_sec > te->when_sec ||
			(now_sec == te->when_sec && now_ms >= te->when_ms)) {
			int retval;
			te = min_heap_pop(&eventLoop->heap);
			/* delete it first */
			aeDeleteTimeEvent(eventLoop, te);
			retval = te->timeProc(eventLoop, te->clientData);
			processed++;
		    /* After an event is processed our time event list may
		     * no longer be the same, so we restart from head.
		     * Still we make sure to don't process events registered
		     * by event handlers itself in order to don't loop forever.
		     * To do so we saved the max ID we want to handle.
		     *
		     * FUTURE OPTIMIZATIONS:
		     * Note that this is NOT great algorithmically. Redis uses
		     * a single time event so it's not a problem but the right
		     * way to do this is to add the new elements on head, and
		     * to flag deleted elements in a special way for later
		     * deletion (putting references to the nodes to delete into
		     * another linked list). */
			if (retval != AE_NOMORE)
				aeModifyTimeEvent(eventLoop, retval, te);
		} else
			break;
    }
    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */
int aeProcessEvents(aeEventLoop *eventLoop, int flags)
{
	int processed = 0, numevents;

    /* Nothing to do? return ASAP */
	if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS))
		return 0;

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
	if (eventLoop->maxfd != -1 ||
		((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
		int j;
		aeTimeEvent *shortest = NULL;
		struct timeval tv, *tvp;

		if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
			shortest = min_heap_top(&eventLoop->heap);	
			
		if (shortest) {
			long now_sec, now_ms;

			/* Calculate the time missing for the nearest
			* timer to fire. */
			aeGetTime(&now_sec, &now_ms);
			tvp = &tv;
			tvp->tv_sec = shortest->when_sec - now_sec;
			if (shortest->when_ms < now_ms) {
				tvp->tv_usec =
					((shortest->when_ms + 1000) - now_ms) * 1000;
				tvp->tv_sec--;
			} else {
				tvp->tv_usec = (shortest->when_ms - now_ms) * 1000;
			}
			if (tvp->tv_sec < 0)
				tvp->tv_sec = 0;
			if (tvp->tv_usec < 0)
				tvp->tv_usec = 0;
		} else {
		    /* If we have to check for events but need to return
		     * ASAP because of AE_DONT_WAIT we need to set the timeout
		     * to zero */
			if (flags & AE_DONT_WAIT) {
				tv.tv_sec = tv.tv_usec = 0;
				tvp = &tv;
			} else {
				/* Otherwise we can block */
				tvp = NULL;	/* wait forever */
			}
		}

		numevents = aeApiPoll(eventLoop, tvp);
		for (j = 0; j < numevents; j++) {
			aeFileEvent *fe = &eventLoop->events[eventLoop->fired[j].fd];
			int mask = eventLoop->fired[j].mask;
			int fd = eventLoop->fired[j].fd;
			int rfired = 0;

		    /* note the fe->mask & mask & ... code: maybe an already processed
		     * event removed an element that fired and we still didn't
		     * processed, so we check if the event is still valid. 
		     *	zhangl it's important 
		     *  first process read evnet ,after write event
		     */
			if (fe->mask & mask & AE_READABLE) {
				rfired = 1;
				fe->rfileProc(eventLoop, fd, fe->clientData, mask);
			}
			if (fe->mask & mask & AE_WRITABLE) {
				if (!rfired || fe->wfileProc != fe->rfileProc)
					fe->wfileProc(eventLoop, fd, fe->clientData, mask);
			}
			processed++;
		}
	}

	/* Check time events */
	if (flags & AE_TIME_EVENTS)
		processed += processTimeEvents(eventLoop);

	return processed;	/* return the number of processed file/time events */
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception */
int aeWait(int fd, int mask, long long milliseconds)
{
	struct pollfd pfd;
	int retmask = 0, retval;

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = fd;
	if (mask & AE_READABLE)
		pfd.events |= POLLIN;
	if (mask & AE_WRITABLE)
		pfd.events |= POLLOUT;

	if ((retval = poll(&pfd, 1, milliseconds)) == 1) {
		if (pfd.revents & POLLIN)
			retmask |= AE_READABLE;
		if (pfd.revents & POLLOUT)
			retmask |= AE_WRITABLE;
		if (pfd.revents & POLLERR)
			retmask |= AE_WRITABLE;
		if (pfd.revents & POLLHUP)
			retmask |= AE_WRITABLE;
		return retmask;
	} else {
		return retval;
	}
}

void aeMain(aeEventLoop *eventLoop)
{
	eventLoop->stop = 0;
	while (!eventLoop->stop) {
		if (eventLoop->beforesleep != NULL)
			eventLoop->beforesleep(eventLoop);
		aeProcessEvents(eventLoop, AE_ALL_EVENTS);
	}
}

char *aeGetApiName(void)
{
	return aeApiName();
}

void aeSetBeforeSleepProc(aeEventLoop *eventLoop,
			  aeBeforeSleepProc *beforesleep)
{
	eventLoop->beforesleep = beforesleep;
}

ssize_t tread(int fd, void *buf, size_t nbytes, unsigned int timout)
{
	int	nfds;
	fd_set	readfds;
	struct timeval	tv;

	tv.tv_sec = timout;
	tv.tv_usec = 0;

	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	nfds = select(fd + 1, &readfds, NULL, NULL, &tv);
	if (nfds <= 0) {
		if (nfds == 0)
			errno = ETIMEDOUT;
		return -1;
	}

	return read(fd, buf, nbytes);
}


ssize_t treadn(int fd, void *buf, size_t nbytes, unsigned int timout)
{
	size_t	nleft;
	ssize_t	nread;
	char *ptr = (char *)buf;

	nleft = nbytes;
	while (nleft > 0) {
		if ((nread = tread(fd, ptr, nleft, timout)) < 0) {
			if (nleft == nbytes) {
				return -1; /* error, return -1 */
			} else {
				break;      /* error, return amount read so far */
			}
		} else if (nread == 0) {
			break;          /* EOF */
		}
		nleft -= nread;
		ptr += nread;
	}

	return nbytes - nleft;      /* return >= 0 */
}

/* Write "n" bytes to a descriptor  */
ssize_t writen(int fd, const void *buf, size_t n)
{
	size_t	nleft;
	ssize_t	nwritten;
	const char *ptr = (const char *)buf;

	nleft = n;
	while (nleft > 0) {
		if ((nwritten = write(fd, ptr, nleft)) < 0) {
			if (nleft == n)	{
				return -1; /* error, return -1 */
			} else {
				break;      /* error, return amount written so far */
			}
		} else if (nwritten == 0) {
			break;
		}
		nleft -= nwritten;
		ptr   += nwritten;
	}

	return n - nleft;      /* return >= 0 */
}

int setnonblock(int sfd)
{
	int flags;

	if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 ||
		fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
		return -1;
	}

	return 0;
}


