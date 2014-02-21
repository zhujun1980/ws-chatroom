/*
 * epoll_event.h
 *
 * Created on: 2011-5-6
 * Author: zhujun
 */

#ifndef EPOLL_EVENT_H_
#define EPOLL_EVENT_H_

#include "event/event.h"

Event* epoll_event_create();
int epoll_event_init(Event* thiz);
int epoll_event_add(Event* thiz, int event, int fd, EventCallback callback, void* arg);
int epoll_event_del(Event* thiz, int event, int fd);
int epoll_event_mod(Event* thiz, int event, int fd);
int epoll_event_dispatch(Event* thiz);
int epoll_event_destroy(Event* thiz);

#endif /* EPOLL_EVENT_H_ */
