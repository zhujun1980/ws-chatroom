/*
 * epoll_event.c
 *
 * Created on: 2011-5-6
 * Author: zhujun
 */
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

#include "alloc.h"
#include "list.h"
#include "event/epoll_event.h"

#define MAX_SIZE 99999

struct _EpollEvent {
	int epoll_fd;
};
typedef struct _EpollEvent EpollEvent;

Event* epoll_event_create() {
	Event* ev = Malloc(sizeof(Event) + sizeof(EpollEvent));
	if(!ev)
	{
		return NULL;
	}
	ev->init = epoll_event_init;
	ev->add = epoll_event_add;
	ev->del = epoll_event_del;
	ev->mod = epoll_event_mod;
	ev->dispatch = epoll_event_dispatch;
	ev->destroy = epoll_event_destroy;

	EpollEvent* ee = (EpollEvent*)ev->priv;
	ee->epoll_fd = epoll_create(MAX_SIZE);
	if(ee->epoll_fd < 0) {
		Free(ev);
		return NULL;
	}
	return ev;
}

int epoll_event_init(Event* thiz) {
	assert(thiz!=NULL);
	return 0;
}

int epoll_event_add(Event* thiz, int event, int fd, EventCallback callback, void* arg) {
	assert(thiz!=NULL);
	return 0;
}

int epoll_event_del(Event* thiz, int event, int fd) {
	assert(thiz!=NULL);
	return 0;
}

int epoll_event_mod(Event* thiz, int event, int fd) {
	assert(thiz!=NULL);
	return 0;
}

int epoll_event_dispatch(Event* thiz) {
	assert(thiz!=NULL);
	return 0;
}

int epoll_event_destroy(Event* thiz) {
	assert(thiz!=NULL);
}
