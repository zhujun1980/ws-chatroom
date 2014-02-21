#ifndef WSCHAT_CORE_H
#define WSCHAT_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <netdb.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#ifdef Darwin
#include <sys/event.h>
#endif

#ifdef Linux
#include <sys/epoll.h>
#endif

#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include "buffer.h"

#define DEF_PORT 		8008
#define MAXSIZE 		1024
#define MAXCLIENT 		8192
#define MAXLISTENING 	10 
#define MAXCONTENT 		8192 
#define SERVERNAME 		"WSCHAT" 
#define SERVERVER 		"0.1.1"

#endif

