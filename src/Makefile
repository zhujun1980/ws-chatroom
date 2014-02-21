uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

OPTIMIZATION?=-O2
STD=-std=c99 -pedantic
WARN=-Wall
OPT=$(OPTIMIZATION)
DEBUG=-g -ggdb

CFLAGS=$(STD) $(WARN) $(OPT) $(DEBUG) -D$(uname_S)

WSOBJS=buffer.o

ws:$(WSOBJS) ws.c core.h buffer.h
	$(CC) -o $@ $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) ws.c $(WSOBJS)

.PHONY:clean
clean:
	rm -rf ws
	find . -name "*.o" | xargs rm -rf

