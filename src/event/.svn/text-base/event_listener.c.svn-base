/*
 * event_listener.c
 *
 *  Created on: 2011-5-1
 *      Author: zhujun
 */
#include "alloc.h"
#include "event/event_listener.h"

EventListener* event_listener_create(int fd, EventCallback callback, void* arg) {
	EventListener* listener = Malloc(sizeof(EventListener));
	if(listener) {
		listener->fd = fd;
		listener->callback = callback;
		listener->arg = arg;
	}
	return listener;
}

void event_listener_destroy(EventListener* listener) {
	if(listener) {
		Free(listener);
	}
}
