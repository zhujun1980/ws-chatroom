/*
 * event.h
 *
 * Created on: 2011-4-19
 * Author: zhujun
 */

#ifndef EVENT_H_
#define EVENT_H_

#include "event/event_listener.h"

#define EVENT_READ		0x00000001
#define EVENT_WRITE		0x00000010
#define EVENT_ERROR		0x00000100
#define EVENT_TIMEOUT	0x00001000
//TODO ADD Persist support
#define EVENT_PERSIST	0x10000000

struct _Event;
typedef struct _Event Event;

typedef int(*event_init)(Event* thiz);
typedef int(*event_add)(Event* thiz, int event, int fd, EventCallback callback, void* arg);
typedef int(*event_del)(Event* thiz, int event, int fd);
typedef int(*event_mod)(Event* thiz, int event, int fd);
typedef int(*event_dispatch)(Event* thiz);
typedef int(*event_destroy)(Event* thiz);

struct _Event {
	event_init init;
	event_add add;
	event_del del;
	event_mod mod;
	event_dispatch dispatch;
	event_destroy destroy;
	char priv[0];
};

#define max(x,y) ((x)>(y) ? (x) : (y))
#define min(x,y) ((x)<(y) ? (x) : (y))

#endif /* EVENT_H_ */
