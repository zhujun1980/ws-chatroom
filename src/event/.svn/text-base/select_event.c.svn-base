/*
 * select_event.c
 *
 * Created on: 2011-4-28
 * Author: zhujun
 */

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

#include "alloc.h"
#include "list.h"
#include "event/select_event.h"

struct _SelectEvent {
	fd_set rset;
	fd_set wset;
	fd_set eset;

	int max_fd;
	DList* rlist;
	DList* wlist;
	DList* elist;
};
typedef struct _SelectEvent SelectEvent;

static void ev_listener_destroy(void* ctx, void* data) {
	EventListener* listener = (EventListener*)data;
	event_listener_destroy(listener);
}

static int ev_listener_cmp_fd(void* ctx, void* data) {
	EventListener* listener = (EventListener*)data;
	int fd = *((int*)ctx);
	if(fd==listener->fd) {
		return 0;
	}
	return -1;
}

static int ev_listener_print_visit(void* ctx, void* data) {
	EventListener* listener = (EventListener*)data;

	fprintf(stderr, "\t\tlist: %d\n", listener->fd);
	return 0;
}

static int ev_listener_add(DList* list, int fd, EventCallback callback, void* arg) {
	int index = dlist_find(list, ev_listener_cmp_fd, &fd);
	if (index > 0) {
		return -2;
	}
	EventListener* listener = event_listener_create(fd, callback, arg);
	if(!listener) {
		return -1;
	}
	dlist_append(list, listener);
	//dlist_foreach(list, ev_listener_print_visit, NULL);
	return 0;
}

static int ev_listener_del(DList* list, int fd) {
	int index = dlist_find(list, ev_listener_cmp_fd, &fd);
	if (index==-1) {
		return -2;
	}
	EventListener * data;
	dlist_get_by_index(list, index, (void**)&data);
	dlist_delete(list, index);
	return 0;
}

static int ev_listener_visit(void* ctx, void* data, int evt) {
	EventListener* listener = (EventListener*)data;
	Event* ev = (Event*)ctx;
	SelectEvent* se = (SelectEvent*)ev->priv;

	switch (evt)
	{
	case EVENT_READ:
		if(FD_ISSET(listener->fd, &(se->rset))) {
			listener->callback(listener->fd, EVENT_READ, listener->arg);
		}
		break;
	case EVENT_WRITE:
		if(FD_ISSET(listener->fd, &(se->wset))) {
			listener->callback(listener->fd, EVENT_WRITE, listener->arg);
		}
		break;
	case EVENT_ERROR:
		if(FD_ISSET(listener->fd, &(se->eset))) {
			listener->callback(listener->fd, EVENT_ERROR, listener->arg);
		}
		break;
	}
	return 0;
}

static int ev_listener_visit_r(void* ctx, void* data) {
	return ev_listener_visit(ctx, data, EVENT_READ);
}

static int ev_listener_visit_w(void* ctx, void* data) {
	return ev_listener_visit(ctx, data, EVENT_WRITE);
}

static int ev_listener_visit_e(void* ctx, void* data) {
	return ev_listener_visit(ctx, data, EVENT_ERROR);
}

Event* select_event_create() {
	Event* ev = Malloc(sizeof(Event) + sizeof(SelectEvent));
	if(!ev)
	{
		return NULL;
	}
	ev->init = select_event_init;
	ev->add = select_event_add;
	ev->del = select_event_del;
	ev->mod = select_event_mod;
	ev->dispatch = select_event_dispatch;
	ev->destroy = select_event_destroy;

	SelectEvent* se = (SelectEvent*)ev->priv;
	FD_ZERO(&se->rset);
	FD_ZERO(&se->wset);
	FD_ZERO(&se->eset);
	se->max_fd = 0;

	se->rlist = dlist_create(ev_listener_destroy, NULL);
	se->wlist = dlist_create(ev_listener_destroy, NULL);
	se->elist = dlist_create(ev_listener_destroy, NULL);
	return ev;
}

int select_event_init(Event* thiz) {
	assert(thiz!=NULL);
	return 0;
}

int select_event_add(Event* thiz, int event, int fd, EventCallback callback, void* arg) {
	assert(thiz!=NULL);
	SelectEvent* se = (SelectEvent*)thiz->priv;
	int ret;

	if(event & EVENT_READ) {
		ret = ev_listener_add(se->rlist, fd, callback, arg);
		assert(ret==0);
	}
	if(event & EVENT_WRITE) {
		ret = ev_listener_add(se->wlist, fd, callback, arg);
		assert(ret==0);
	}
	if(event & EVENT_ERROR) {
		ret = ev_listener_add(se->elist, fd, callback, arg);
		assert(ret==0);
	}
	return 0;
}

int select_event_del(Event* thiz, int event, int fd) {
	assert(thiz!=NULL);
	SelectEvent* se = (SelectEvent*)thiz->priv;

	if(event & EVENT_READ) {
		ev_listener_del(se->rlist, fd);
	}
	if(event & EVENT_WRITE) {
		ev_listener_del(se->wlist, fd);
	}
	if(event & EVENT_ERROR) {
		ev_listener_del(se->elist, fd);
	}
	return 0;
}

int select_event_mod(Event* thiz, int event, int fd) {
	assert(thiz!=NULL);
	return 0;
}

static int ev_listener_visit_set_fd(void* ctx, void* data, int evt) {
	//assert(thiz!=NULL);
	Event* ev = (Event*)ctx;
	SelectEvent* se = (SelectEvent*)ev->priv;
	EventListener* listener = (EventListener*)data;

	se->max_fd = max(se->max_fd, listener->fd);
	switch(evt)
	{
		case EVENT_READ:
			FD_SET(listener->fd, &(se->rset));
			break;
		case EVENT_WRITE:
			FD_SET(listener->fd, &(se->wset));
			break;
		case EVENT_ERROR:
			FD_SET(listener->fd, &(se->eset));
			break;
	}
	return 0;
}

static int ev_listener_visit_set_rfd(void* ctx, void* data) {
	return ev_listener_visit_set_fd(ctx, data, EVENT_READ);
}

static int ev_listener_visit_set_wfd(void* ctx, void* data) {
	return ev_listener_visit_set_fd(ctx, data, EVENT_WRITE);
}

static int ev_listener_visit_set_efd(void* ctx, void* data) {
	return ev_listener_visit_set_fd(ctx, data, EVENT_ERROR);
}

int select_event_dispatch(Event* thiz) {
	assert(thiz!=NULL);
	SelectEvent* se = (SelectEvent*)thiz->priv;
	fd_set *ra, *wa, *ea;
	int rlen, wlen, elen;

	while(1) {
		ra = wa = ea = NULL;
		FD_ZERO(&se->rset);
		FD_ZERO(&se->wset);
		FD_ZERO(&se->eset);
		rlen = dlist_length(se->rlist);
		wlen = dlist_length(se->wlist);
		elen = dlist_length(se->elist);

		if(rlen > 0) {
			dlist_foreach(se->rlist, ev_listener_visit_set_rfd, thiz);
			ra = &se->rset;
		}
		if(wlen > 0) {
			dlist_foreach(se->wlist, ev_listener_visit_set_wfd, thiz);
			wa = &se->wset;
		}
		if(elen > 0) {
			dlist_foreach(se->elist, ev_listener_visit_set_efd, thiz);
			ea = &se->eset;
		}
		int num = select(se->max_fd + 1, ra, wa, ea, 0);
		if(num < 0) {
		}
		else if (num ==0) {
		}
		else {
			if(rlen > 0) {
				dlist_foreach(se->rlist, ev_listener_visit_r, thiz);
			}
			if(wlen > 0) {
				dlist_foreach(se->wlist, ev_listener_visit_w, thiz);
			}
			if(elen > 0) {
				dlist_foreach(se->elist, ev_listener_visit_e, thiz);
			}
		}
	}
	return 0;
}

int select_event_destroy(Event* thiz) {
	assert(thiz!=NULL);
	Free(thiz);
	return 0;
}
