/*
 * select_event.h
 *
 * Created on: 2011-4-28
 * Author: zhujun
 */

#ifndef SELECT_EVENT_H_
#define SELECT_EVENT_H_

#include "event/event.h"

Event* select_event_create();
int select_event_init(Event* thiz);
int select_event_add(Event* thiz, int event, int fd, EventCallback callback, void* arg);
int select_event_del(Event* thiz, int event, int fd);
int select_event_mod(Event* thiz, int event, int fd);
int select_event_dispatch(Event* thiz);
int select_event_destroy(Event* thiz);

#endif /* SELECT_EVENT_H_ */
