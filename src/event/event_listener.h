/*
 * event_listener.h
 *
 *  Created on: 2011-5-1
 *      Author: zhujun
 */

#ifndef EVENT_LISTENER_H_
#define EVENT_LISTENER_H_

typedef void(*EventCallback)(int fd, int event, void* args);

struct _EventListener {
	int fd;
	EventCallback callback;
	void* arg;
};
typedef struct _EventListener EventListener;

EventListener* event_listener_create(int fd, EventCallback callback, void* arg);
void event_listener_destroy(EventListener* listener);

#endif /* EVENT_LISTENER_H_ */
