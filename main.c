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
#include <sys/epoll.h>
#include <sys/queue.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#define DEF_PORT 8008

//TODO Change to 1024
#define MAXSIZE 1024
#define MAXCLIENT 8192
#define MAXLISTENING 10 
#define MAXCONTENT 8192 
#define SERVERNAME "NonBlocking" 
#define SERVERVER "0.1.1"

#define START_HEADER "HTTP/1.1 200 OK\r\n" \
		     "Server: " SERVERNAME "/" SERVERVER "\r\n" \
    		     "Connection: close\r\n" \
		     "Cache-Control: no-cache, must-revalidate \r\n" \
		     "Content-Type: text/html\r\n"	

#define START_PAGE   "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n" \
		     "<head>\n" \
		     "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />\n" \
		     "<title>Work</title>\n" \
		     "</head>\n" \
		     "<body>\n" \
		     "<H1>It works!</H1>\n" \
		     "</body>\n" \
		     "</html>\n"

#define WEBSOCKET_UUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

#define WS_FRAME_FIN_AND_RSV 		0x80

#define WS_FRAME_OPCODE_CONTINUATION	0
#define WS_FRAME_OPCODE_TEXT		1
#define WS_FRAME_OPCODE_BINARY		2
#define WS_FRAME_OPCODE_CLOSE		8
#define WS_FRAME_OPCODE_PING		9
#define WS_FRAME_OPCODE_PONG		10

#define WS_FRAME_MASK			0

typedef void (*Sigfunc)(int);

const char *state_1xx_strs[] = {
	"Continue",		//100
	"Switching Protocols"	//101
};

const char *state_2xx_strs[] = {
	"OK",			//200
	"Created",		//201
	"Accepted",		//202
	"Non-Authoritative Information",//203
	"No Content",		//204
	"Reset Content",	//205
	"Partial Content",	//206
	"Multi-Status",		//207
};

const char *state_3xx_strs[] = {
	"Multiple Choices",	//300
	"Moved Permanently",	//301
	"Found",		//302
	"See Other",		//303
	"Not Modified",		//304
	"Use Proxy",		//305
	"Unused", 		//306
	"Temporary Redirect",	//307
};

const char *state_4xx_strs[] = {
	"Bad Request", 		//400
	"Unauthorized",		//401
	"Payment Required", 	//402
	"Forbidden",		//403
	"Not Found",		//404
	"Method Not Allowed",	//405
	"Not Acceptable",	//406
	"Proxy Authentication Required",//407
	"Request Timeout", 	//408
	"Conflict", 		//409
	"Gone",			//410
	"Length Required",	//411
	"Precondition Failed",	//412
	"Request Entity Too Large",//413
	"Request-URI Too Long", //414
	"Unsupported Media Type",//415
	"Requested Range Not Satisfiable",//416
	"Expectation Failed", 	//417
};

const char *state_5xx_strs[] = {
	"Internal Server Error", //500
	"Not Implemented",	//501
	"Bad Gateway",		//502
	"Service Unavailable",	//503
	"Gateway Timeout",	//504
	"HTTP Version Not Supported",//505
};

const char **state_strs[] = {
	state_1xx_strs,
	state_2xx_strs,
	state_3xx_strs,
	state_4xx_strs,
	state_5xx_strs,
};

LIST_HEAD(http_header, headerkv);

struct headerkv {
	char *key;
	char *val;
	LIST_ENTRY(headerkv) entries;
};

char* method_strs[] = {
	"HEAD",
	"GET",
	"POST",
	"PUT",
	"DELETE",
};

enum METHOD {
	HEAD,
	GET,
	POST,
	PUT,
	DELETE,
};

char* http_ver_strs[] = {
	"HTTP/1.0",
	"HTTP/1.1",
};

enum HTTPVER {
	HTTP1,
	HTTP2,
};

struct nb_buffer {
	char *raw_data;
	int data_len;
	int buf_len;
};

struct ws_frame {
	short bit_fin;
	short bit_rsv1;
	short bit_rsv2;
	short bit_rsv3;
	short opcode;
	short bit_mask;
	__uint64_t length;
	unsigned int masking_key;
	struct nb_buffer ws_buffer;
};

struct request {
	enum METHOD method;
	enum HTTPVER version;
	char* url;
	char* raw_data;
	int data_len;
	int buf_len;

	struct nb_buffer res_buffer;

	struct ws_frame ws_data;

	struct http_header req_header;
	struct http_header res_header;
};

enum STATE {
	HEADER_PARSE,
	HEADER_FIN,
	MESSAGING,
};

struct client {
	enum STATE state;
	int client_socket;
	struct request* req;
	struct epoll_event *ev;
};

char* server_host;
char* htdocs_dir;
int server_port = -1;
int event_fd;
int serial_number = 1;

int server_sockets[MAXLISTENING + 1];
char clients_set[MAXCLIENT];
struct client* clients[MAXCLIENT];

void ws_send_frame(struct client *client, char *payload);
void ws_boardcast_frame(struct client *client, char *payload);

static void usage(void) {
    printf( "-p <num>      TCP port number to listen on (default: 8008)\n"
            "-l <ip_addr>  interface to listen on (default: INADDR_ANY, all addresses)\n"
            //"-a <htdocs>   Htdocs dir\n"
            "-h            print this help and exit\n"
	    "\n" );
}

static uint64_t mc_swap64(uint64_t in) {
    /* Little endian, flip the bytes around until someone makes a faster/better
    * way to do this. */
    int64_t rv = 0;
    int i = 0;
     for(i = 0; i<8; i++) {
        rv = (rv << 8) | (in & 0xff);
        in >>= 8;
     }
    return rv;
}

uint64_t ntohll(uint64_t val) {
   return mc_swap64(val);
}

uint64_t htonll(uint64_t val) {
   return mc_swap64(val);
}

const char *html_replace(char ch, char *buf) {
	switch (ch) {
	case '<':
		return "&lt;";
	case '>':
		return "&gt;";
	case '"':
		return "&quot;";
	case '\'':
		return "&#039;";
	case '&':
		return "&amp;";
	default:
		break;
	}

	/* Echo the character back */
	buf[0] = ch;
	buf[1] = '\0';
	
	return buf;
}

char *htmlescape(const char *html) {
	int i, new_size = 0, old_size = strlen(html);
	char *escaped_html, *p;
	char scratch_space[2];
	
	for (i = 0; i < old_size; ++i)
        	new_size += strlen(html_replace(html[i], scratch_space));

	p = escaped_html = malloc(new_size + 1);
	for (i = 0; i < old_size; ++i) {
		const char *replaced = html_replace(html[i], scratch_space);
		/* this is length checked */
		strcpy(p, replaced);
		p += strlen(replaced);
	}
	*p = '\0';

	return (escaped_html);
}

char *base64(const unsigned char *input, int length) {
	BIO *bmem, *b64;
	BUF_MEM *bptr;

	b64 = BIO_new(BIO_f_base64());
	bmem = BIO_new(BIO_s_mem());
	b64 = BIO_push(b64, bmem);
	BIO_write(b64, input, length);
	BIO_flush(b64);
	BIO_get_mem_ptr(b64, &bptr);

	char *buff = (char *)malloc(bptr->length);
	memcpy(buff, bptr->data, bptr->length-1);
	buff[bptr->length-1] = 0;

	BIO_free_all(b64);
	return buff;
}

void sha1(const char *input, int length, unsigned char hash[20]) {
	SHA_CTX s;

	SHA1_Init(&s);
	SHA1_Update(&s, input, length);
	SHA1_Final(hash, &s);
}

void nb_buffer_init(struct nb_buffer *buffer) {
	buffer->raw_data = 0;
	buffer->data_len = 0;
	buffer->buf_len = 0;
}

void nb_buffer_alloc(struct nb_buffer *buffer, int size) {
	buffer->raw_data = (char *)malloc(sizeof(char) * (size + 1));
	buffer->data_len = 0;
	buffer->buf_len = size;
}

void nb_buffer_free(struct nb_buffer *buffer) {
	free(buffer->raw_data);
	nb_buffer_init(buffer);
}

void nb_buffer_clean(struct nb_buffer *buffer) {
	buffer->data_len = 0;
	buffer->raw_data[0] = '\0';
}

void nb_buffer_realloc(struct nb_buffer *buffer, int new_size) {
	if(new_size <= buffer->buf_len)
		return;
	buffer->raw_data = (char *)realloc(buffer->raw_data, sizeof(char) * (new_size + 1));
	buffer->buf_len = new_size;
}

void nb_buffer_add(struct nb_buffer *buffer, size_t nread) {
	buffer->data_len += nread;
	buffer->raw_data[buffer->data_len] = '\0';
}

char* nb_buffer_get(struct nb_buffer* buffer, size_t buf_len) {
	if( !buffer->raw_data ) {
		nb_buffer_alloc(buffer, buf_len);
		return buffer->raw_data;
	}

	if ((buffer->buf_len - buffer->data_len) < buf_len) {
		nb_buffer_realloc(buffer, buffer->buf_len + buf_len);
		buffer->raw_data[buffer->data_len] = '\0';
	}
	return buffer->raw_data + buffer->data_len;
}

void nb_buffer_memcpy(struct nb_buffer *buffer,  void *byte, size_t length) {
	char *buf_free;

	if(!buffer->raw_data) {
		nb_buffer_alloc(buffer, MAXSIZE);
	}

	if((buffer->buf_len - buffer->data_len) < length)
		nb_buffer_realloc(buffer, buffer->buf_len + length);

	buf_free = buffer->raw_data + buffer->data_len;

	memcpy(buf_free, byte, length);
	buffer->data_len += length;
	buffer->raw_data[buffer->data_len] = '\0';
}

int nb_buffer_append(struct nb_buffer *buffer, char *data) {
	int length, free_size, nwrite;
	char *buf_free;

	length = strlen(data);

	if(!buffer->raw_data)
		nb_buffer_alloc(buffer, MAXSIZE);

	free_size = buffer->buf_len - buffer->data_len;
	if(free_size < length) {
		nb_buffer_realloc(buffer, buffer->buf_len + length);
		free_size = buffer->buf_len - buffer->data_len;
	}
	buf_free = buffer->raw_data + buffer->data_len;
	nwrite = snprintf(buf_free, free_size, "%s", data);	
	buffer->data_len += nwrite;
	buffer->raw_data[buffer->data_len] = '\0';
	return nwrite;
}

int nb_buffer_printf(struct nb_buffer *buffer, const char *fmt, ...) {
	va_list ap;
	char *buf_free;
	int free_size, nwrite;

	if(!buffer->raw_data) {
		nb_buffer_alloc(buffer, MAXSIZE);
	}
	free_size = buffer->buf_len - buffer->data_len;
	if(free_size == 0) {
		nb_buffer_realloc(buffer, buffer->buf_len * 2);
		free_size = buffer->buf_len - buffer->data_len;
	}
	buf_free = buffer->raw_data + buffer->data_len;

	va_start(ap, fmt);
	while(1) {
		nwrite = vsnprintf(buf_free, free_size + 1, fmt, ap); 
		if (nwrite >= free_size + 1) { //Last byte is guard
			buffer->raw_data[buffer->data_len] = '\0';
			nb_buffer_realloc(buffer, buffer->buf_len * 2);
			free_size = buffer->buf_len - buffer->data_len;
			buf_free = buffer->raw_data + buffer->data_len;
		}
		else {
			buffer->data_len += nwrite;
			buffer->raw_data[buffer->data_len] = '\0';
			break;
		}
	}
	va_end(ap);
	return nwrite;
}

const char *state_str_get(int http_state) {
	int i1, i2;

	if(http_state < 100 || http_state >= 600)
		return NULL;

	i1 = http_state / 100;
	i2 = http_state % (i1 * 100);

	//if(i2 < sizeof(state_strs[i1 - 1]))
	return state_strs[i1 - 1][i2];
}

char* header_get(struct  http_header *header_head, const char* key) {
	struct headerkv *np;

	np = header_head->lh_first;
	for(np = header_head->lh_first; np != NULL; np = np->entries.le_next)
		if(strcmp(np->key, key) == 0)
			return np->val;
	return  NULL;
}

const char *header_default_get(struct client *client, const char *key, const char *default_val) {
	char *head_val;

	head_val = header_get(&client->req->req_header, key);
	if(!head_val)
		return default_val;
	return head_val;
}

int header_equal(struct client *client, const char *key, const char *val) {
	char *head_val;

	head_val = header_get(&client->req->req_header, key);
	if(!head_val)
		return -1;
	return strcasecmp(head_val, val);
}

void header_set(struct client *client, const char *key, const char *val) {
	struct headerkv *hkv, *np;
	struct http_header *hheader;

	hheader = &(client->req->res_header);

	for(np = hheader->lh_first; np != NULL; np = np->entries.le_next)
		if(strcmp(np->key, key) == 0) {
			free(np->val);
			np->val = strdup(val);
			return;
		}

	//Not Found
	hkv = (struct headerkv *) malloc(sizeof(struct headerkv));
	hkv->key = strdup(key);
	hkv->val = strdup(val);
	LIST_INSERT_HEAD(hheader, hkv, entries);
}

void header_list_rm(struct client *client) {
	struct request *req;
	struct headerkv *np;

	req = client->req;

	for(np = req->req_header.lh_first; np != NULL; np = np->entries.le_next)
		free(np);
	while(req->req_header.lh_first != NULL)
		LIST_REMOVE(req->req_header.lh_first, entries);

	for(np = req->res_header.lh_first; np != NULL; np = np->entries.le_next) {
		free(np->key);
		free(np->val);
		free(np);
	}
	while(req->res_header.lh_first != NULL)
		LIST_REMOVE(req->res_header.lh_first, entries);
}

struct client* new_client(int client_socket) {
	clients_set[client_socket] = '1';
	if(!clients[client_socket])
		clients[client_socket] = (struct client*) malloc(sizeof(struct client));
	if(clients[client_socket]) {
		clients[client_socket]->state = HEADER_PARSE;
		clients[client_socket]->client_socket = client_socket;
		clients[client_socket]->req = 0;
		clients[client_socket]->ev = 0;
	}
	return clients[client_socket];
}

void free_client(int client_socket) {
	clients_set[client_socket] = '\0';
	if(clients[client_socket]->req) {
		header_list_rm(clients[client_socket]);
		if(clients[client_socket]->req->raw_data) {
			free(clients[client_socket]->req->raw_data);
			clients[client_socket]->req->raw_data = 0;
		}
		free(clients[client_socket]->req);
		clients[client_socket]->req = 0;
	}
	free(clients[client_socket]);
	clients[client_socket] = 0;
}

int setnonblock(int fd) {
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
    	//errlog(ERROR_LEVEL_ERROR, "fcntl");
		return flags;
    }
    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) < 0) {
    	//errlog(ERROR_LEVEL_ERROR, "fcntl");
        return -1;
    }
    return 0;
}

static int new_socket(struct addrinfo *ai) {
    int sfd;
    if ((sfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
        return -1;
    }
    if (setnonblock(sfd) < 0) {
        close(sfd);
        return -1;
    }
    return sfd;
}

static int tcp_server_socket() {
    int server_socket;
    struct addrinfo *ai;
    struct addrinfo *next;
    struct addrinfo hints = { .ai_flags = AI_PASSIVE,
                              .ai_family = AF_UNSPEC,
		    	      .ai_socktype = SOCK_STREAM};
    int error;
    int last;
    int flags = 1;
    struct linger ling = {0, 0};
    char port_buf[NI_MAXSERV];

    snprintf(port_buf, sizeof(port_buf), "%d", server_port);
    error= getaddrinfo(server_host, port_buf, &hints, &ai);
    if (error != 0) {
        if (error != EAI_SYSTEM)
        	printf("getaddrinfo(): %s\n", gai_strerror(error));
        else
        	printf("getaddrinfo()");
        return -1;
    }

    for(last=0, next = ai; next; next = next->ai_next) {
        if((server_socket = new_socket(next)) == -1) {
        	printf("create socket failed");
            freeaddrinfo(ai);
            return -1;
        }

#ifdef IPV6_V6ONLY
        if (next->ai_family == AF_INET6) {
            error = setsockopt(server_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &flags, sizeof(flags));
            if (error != 0) {
            	printf("setsockopt");
                close(server_socket);
				freeaddrinfo(ai);
                return -1;
            }
        }
#endif

        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (void *)&flags, sizeof(flags));
		error = setsockopt(server_socket, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
		if (error != 0)
			printf("setsockopt 1");

		error = setsockopt(server_socket, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));
		if (error != 0)
			printf("setsockopt 2");

		error = setsockopt(server_socket, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
		if (error != 0)
			printf("setsockopt 3");

		if(bind(server_socket, next->ai_addr, next->ai_addrlen)==-1) {
			printf("bind()");
            close(server_socket);
            freeaddrinfo(ai);
            return -1;
		}
		else {
			if(listen(server_socket, 1024) == -1) {
				printf("listen()");
	            close(server_socket);
	            freeaddrinfo(ai);
	            return -1;
			}

	        struct sockaddr_in addr;
	        size_t add_len = sizeof(addr);
			getsockname(server_socket, (struct sockaddr*)&addr, &add_len);
			char ipAddr[256] = {0};
			inet_ntop(next->ai_addr->sa_family, &addr.sin_addr, ipAddr, sizeof(ipAddr));
			printf("Listening on NO %d %s:%d\n", last, ipAddr, ntohs(addr.sin_port));

			//socket
			if(last >= (sizeof(server_sockets) - 1)) {
				fprintf(stderr, "Too many listen socket");
	            close(server_socket);
	            freeaddrinfo(ai);
	            return -1;
			}
			server_sockets[last] = server_socket;
		}
		last++;
    }
    server_sockets[last] = -1;
    freeaddrinfo(ai);
    return 0;
}

char* client_buffer_get(struct client* client, size_t buf_len) {
	struct request* req;
	int free_len;

	req = client->req;
	if( !req ) {
		//fprintf(stderr, "socket %d, new buffer\n", client->client_socket);
		req = (struct request *) malloc(sizeof(struct request));
		req->raw_data = (char *) malloc(sizeof(char) * (buf_len + 1));
		req->buf_len = buf_len;
		req->data_len = 0;

		nb_buffer_init(&req->res_buffer);
		nb_buffer_init(&req->ws_data.ws_buffer);

		LIST_INIT(&(req->req_header));
		LIST_INIT(&(req->res_header));
		client->req = req;
		return req->raw_data;
	}
	free_len = req->buf_len - req->data_len;
	//fprintf(stderr, "socket %d, use buffer, free_len %d\n", client->client_socket, free_len);
	if (free_len < buf_len) {
		req->buf_len = req->buf_len + buf_len - free_len;
		req->raw_data = (char *)realloc(req->raw_data, sizeof(char) * (req->buf_len + 1));
		client->req = req;
	}
	return req->raw_data + req->data_len;
}

void client_buffer_add(struct client* client, size_t nread) {
	//fprintf(stderr, "%d - %d - %d\n", client->req->data_len, nread, client->req->buf_len);
	client->req->data_len += nread;
	client->req->raw_data[client->req->data_len] = '\0';
	//fprintf(stderr, "%s\n", client->req->raw_data);
}

void client_disconnect(struct client *client) {
	int client_socket;

	client_socket = client->client_socket;

	epoll_ctl(event_fd, EPOLL_CTL_DEL, client_socket, client->ev);
	close(client_socket);
	client->ev->data.ptr = 0;
	free_client(client_socket);
	//fprintf(stderr, "socket %d, disconnect\n", client_socket);
}

/*
void http_response(struct client *client, const char *body) {
	struct request* req;

	req = client->req;
}
*/

void client_addr(int client_socket, char* addr, int addr_len) {
	struct sockaddr_in addr_in;
	size_t len;

	len = sizeof(addr_in);
	if(getsockname(client_socket, (struct sockaddr *)&addr_in, &len) == 0) {
		snprintf(addr, addr_len, "%s", inet_ntoa(addr_in.sin_addr));
	}
}

void get_rfc822_time(char *time_str, int length) {
	struct tm curtime;
	time_t t;
	int nwrite;

	t = time(NULL);
	localtime_r(&t, &curtime);
	nwrite = strftime(time_str, length, "%a, %d %b %Y %H:%M:%S %z", &curtime);
	time_str[nwrite] = '\0';
}

void get_curr_time(char *time_str, int str_len) {
	time_t t;
	struct tm curtime;
	
	t = time(NULL);
	localtime_r(&t, &curtime);
	snprintf(time_str, str_len, "%02d/%02d/%d %02d:%02d:%02d", curtime.tm_mday, curtime.tm_mon + 1, curtime.tm_year+1900, curtime.tm_hour, curtime.tm_min, curtime.tm_sec);
}

int http_get_first_line(struct client *client, char *first_line, int line_size) {
	struct request *req;
	char *raw_data;
	int i;

	req = client->req;
	if(!req) 
		return -1;
	if(!req->raw_data) 
		return -1;

	raw_data = req->raw_data;

	i = 0;
	while(i < line_size && raw_data[i] != '\r' && raw_data[i+1] != '\n') {
		first_line[i] = raw_data[i];
		++i;
	}
	return i;
}

void access_log(struct client *client, int http_state) {
	int client_socket;
	char addr[16] = {0};
	char time_str[33] = {0};

	client_socket = client->client_socket;
	client_addr(client_socket, addr, sizeof(addr) - 1);
	
	get_curr_time(time_str, sizeof(time_str) - 1);
	
	fprintf(stderr, "%s [%s] %d \"%s %s %s\" \"%s\" \"%s\"\n", addr, time_str, http_state, method_strs[client->req->method], client->req->url, http_ver_strs[client->req->version], header_default_get(client, "User-Agent", "-"), header_default_get(client, "Sec-WebSocket-Protocol", "-"));
}

/*
void http_read_file(struct client *client) {
	struct request *req;
	char file_path[MAXSIZE] = {0};
	FILE* file;

	req = client->req;

	snprintf(file_path, sizeof(file_path), "%s/%s", htdocs_dir, req->url);
	file = fopen(file_path, "r");
	if(file) {
		fclose(file);
	}
	
}
*/

void http_return(struct client *client, const char *body) {
	int length, nread, nwrite;
	char response[1024];
	char rfctime[35];

	length = strlen(body);
	get_rfc822_time(rfctime, sizeof(rfctime) - 1);
	nread = snprintf(response, sizeof(response) - 1, "%sDate: %s\r\nContent-Length: %d\r\n\r\n%s", START_HEADER, rfctime, length, START_PAGE);
	nwrite = send(client->client_socket, response, nread, 0);
	//fprintf(stderr, "write %d bytes\n", nwrite);
	access_log(client, 200);
	client_disconnect(client);
}

void http_return_state(struct client *client, int http_state) {
	int nread;
	char response[1024];
	char rfctime[35];

	get_rfc822_time(rfctime, sizeof(rfctime) - 1);

	nread = snprintf(response, sizeof(response) - 1, "HTTP/1.1 %d %s\r\n" \
						     "Server: " SERVERNAME "/" SERVERVER "\r\n" \
						     "Connection: close\r\n" \
						     "Date: %s\r\n" \
						     "Cache-Control: no-cache, must-revalidate \r\n\r\n", \
						     http_state, 
						     state_str_get(http_state),
						     rfctime);

	send(client->client_socket, response, nread, 0);
	access_log(client, http_state);
	client_disconnect(client);
}

void http_header_send(struct client *client, int http_state) {
	int nread;
	struct headerkv *np;
	struct http_header *hheader;
	struct nb_buffer *buffer;

	hheader = &client->req->res_header;
	buffer = &client->req->res_buffer;

	nread = nb_buffer_printf(buffer, "HTTP/1.1 %d %s\r\n" \
					 "Server: " SERVERNAME "/" SERVERVER "\r\n", \
					 http_state, 
					 state_str_get(http_state));

	for(np = hheader->lh_first; np != NULL; np = np->entries.le_next) {
		nread = nb_buffer_printf(buffer, "%s: %s\r\n", np->key, np->val);
	}
	nread = nb_buffer_append(buffer, "\r\n");
	//fprintf(stderr, "%s", buffer->raw_data);
	send(client->client_socket, buffer->raw_data, buffer->data_len, 0);
}

void http_parse_header(struct client *client, char *header_data) {
	struct request *req;
	struct http_header *hheader;
	int i, find;
	char *header_ptr, *header_head;

	req = client->req;
	hheader = &(req->req_header);

	header_ptr = header_data;
	while(1) {
		struct headerkv *hkv = (struct headerkv*) malloc(sizeof(struct headerkv));
		hkv->key = NULL;
		hkv->val = NULL;

		header_head = header_ptr;
		find = 0;
		i = 0;
		while(header_head[i] != '\n') {
			if(!find && header_head[i] == ':') {
				find = 1;
				header_head[i] = '\0';
				
				hkv->key = header_ptr;
				if(header_head[i+1] == ' ')
					header_ptr = header_head + i + 2;
				else
					header_ptr = header_head + i + 1;
			}

			if(header_head[i] == '\r') {
				header_head[i] = '\0';
				hkv->val = header_ptr;
				header_ptr = header_head + i + 1;
			}

			++i;
		}

		header_ptr++; //skip '\n'

		LIST_INSERT_HEAD(hheader, hkv, entries);

		if(*header_ptr == '\r' && *(header_ptr + 1) == '\n') //http header end
			break;
	}

	//TODO POST read content-length

	client->state = HEADER_FIN;
	return;
}

void http_parse(struct client *client) {
	struct request *req;
	char *http_data;
	int i, item;

	req = client->req;
	if( !req ) 
		return;

	http_data = req->raw_data;
	if( !http_data ) 
		return;

	if(strstr(http_data, "\r\n\r\n") == NULL)
		return;

	i = 0;
	item = 0;
	//first line
	while(req->raw_data[i] != '\n') {
		if(req->raw_data[i] == ' ' || req->raw_data[i] == '\r') {
			req->raw_data[i] = '\0';

			if(item == 0) {
				if(strcmp(http_data, "GET") == 0)
					req->method = GET;
				else if(strcmp(http_data, "POST") == 0)
					req->method = POST;
				else
					fprintf(stderr, "unsupport http method %s\n", http_data);
				item++;
			}
			else if(item == 1) {
				req->url = http_data;
				item++;
			}
			else if(item == 2) {
				//fprintf(stderr, "%s\n", http_data);
				if(strcmp(http_data, "HTTP/1.0") == 0)
					req->version = HTTP1;
				else if(strcmp(http_data, "HTTP/1.1") == 0)
					req->version = HTTP2;
				else
					fprintf(stderr, "unsupport http version, %s\n", http_data);
			}
			http_data = req->raw_data + i + 1;
			//fprintf(stderr, "%s\n", http_data);
		}
		++i;
	}
	//fprintf(stderr, "%s %s\n", (req->method == GET) ? "GET" : "POST", req->url);
	http_data++; //skip char '\n';
	http_parse_header(client, http_data);
}

void ws_sec_accept_key(struct client *client) {
	const char* client_key;
	char accept_key[100];
	unsigned char hash[20];
	int nwrite;
	char *base_str;

	client_key = header_get(&client->req->req_header, "Sec-WebSocket-Key");	
	nwrite = snprintf(accept_key, sizeof(accept_key), "%s%s", client_key, WEBSOCKET_UUID);

	sha1(accept_key, nwrite, hash);

	base_str = base64(hash, sizeof(hash));
	header_set(client, "Sec-WebSocket-Accept", base_str);

	free(base_str);
}


void ws_handshake(struct client *client) {
	char rfctime[35];

	if(client->state != HEADER_FIN)
		return;

	if(client->req->version != HTTP2 || 
		header_equal(client, "Connection", "Upgrade") != 0 || 
		header_equal(client, "Sec-WebSocket-Version", "13") != 0) {
		http_return_state(client, 400);
		return;
	}

	get_rfc822_time(rfctime, sizeof(rfctime) - 1);

	header_set(client, "Upgrade", "websocket");
	header_set(client, "Connection", "Upgrade");
	header_set(client, "Date", rfctime);
	ws_sec_accept_key(client);
	http_header_send(client, 101);
	client->state = MESSAGING;

	//default nickname
	char nick[20] = {0};
	snprintf(nick, sizeof(nick) - 1, "User#%d", serial_number++);
	header_set(client, "nick", nick);

	ws_send_frame(client, "Notification> Welcome~");
	char present[51];
	snprintf(present, sizeof(present) - 1, "Notification> %s come in", nick);
	ws_boardcast_frame(client, present);
}

void epoll_write_ev(struct client *client) {
	struct epoll_event ev1;
	ev1.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
	ev1.data.ptr = client;
	epoll_ctl(event_fd, EPOLL_CTL_ADD, client->client_socket, &ev1);
}

void ws_send_frame(struct client *client, char *payload) {
	struct nb_buffer *buffer;
	unsigned char ws_byte;
	size_t length;
	int nwrite;

	if(client->state != MESSAGING)
		return;

	buffer = &client->req->res_buffer;
	nb_buffer_clean(buffer);
	
	ws_byte = 0;
	ws_byte = WS_FRAME_FIN_AND_RSV | WS_FRAME_OPCODE_TEXT;
	nb_buffer_memcpy(buffer, &ws_byte, 1);

	length = strlen(payload);
	//TODO ADD multi-bytes length support, network byte order
	if(length < 126 && length >= 0) {
		ws_byte = WS_FRAME_MASK | length; 
		nb_buffer_memcpy(buffer, &ws_byte, 1);
	}
	else if(length > 125 && length < 65536) {
		ws_byte = WS_FRAME_MASK | 126;
		nb_buffer_memcpy(buffer, &ws_byte, 1);
		short s = htons(length);
		nb_buffer_memcpy(buffer, &s, 2);
	}
	else if(length > 65536) {
		ws_byte = WS_FRAME_MASK | 127;
		nb_buffer_memcpy(buffer, &ws_byte, 1);
		__uint64_t u64 = htonll(length);
		nb_buffer_memcpy(buffer, &u64, 8);
	}

	nb_buffer_memcpy(buffer, (void *)payload, length);

	//epoll_write_ev(client);

	nwrite = send(client->client_socket, buffer->raw_data, buffer->data_len, 0);
	//fprintf(stderr, "%d, %d, %s\n", nwrite, buffer->data_len, buffer->raw_data);
}

void ws_boardcast_frame(struct client *client, char *payload) {
	int idx;

	for(idx = 0; idx < MAXCLIENT; idx++)
		if(clients_set[idx] == '1')
			ws_send_frame(clients[idx], payload);
}

void mask(char *first, char *end, unsigned char *masking_key) {
	int i;
	char *ptr;

	for(i = 0, ptr = first; ptr != end; ptr++, ++i)
		*ptr = *ptr ^ masking_key[i % 4];
}

void ws_close(struct client *client, unsigned short int state) {
	struct nb_buffer *buffer;
	unsigned char ws_byte;

	buffer = &client->req->res_buffer;
	nb_buffer_clean(buffer);
	
	ws_byte = 0;
	ws_byte = WS_FRAME_FIN_AND_RSV | WS_FRAME_OPCODE_CLOSE;
	nb_buffer_memcpy(buffer, &ws_byte, 1);
	ws_byte = 0x80;
	nb_buffer_memcpy(buffer, &ws_byte, 1);

	state = htons(state);
	nb_buffer_memcpy(buffer, &state, 2);
	nb_buffer_memcpy(buffer, &state, 2);
	
	send(client->client_socket, buffer->raw_data, buffer->data_len, 0);
}

__uint64_t ws_length_u64(char *c) {
	__uint64_t *ll = (__uint64_t *)c;
	return ntohll(*ll);
}

short ws_length_short(char *c) {
	unsigned short *s = (unsigned short *)c;
	return ntohs(*s);
}

char *ws_parse(struct client *client) {
	struct ws_frame *frame;
	struct nb_buffer *buffer;
	char *ptr;
	unsigned short length;

	frame = &client->req->ws_data;
	buffer = &frame->ws_buffer;

	if(buffer->data_len == 0)
		return NULL;
	
	//first byte
	ptr = buffer->raw_data;
	if (((*ptr) & WS_FRAME_FIN_AND_RSV) == WS_FRAME_FIN_AND_RSV)
		frame->bit_fin = 1;
	else
		frame->bit_fin = 0;
	frame->bit_rsv1 = 1;
	frame->bit_rsv2 = 1;
	frame->bit_rsv3 = 1;
	frame->opcode = ((*ptr) & 0x0F);
	if(frame->opcode == WS_FRAME_OPCODE_CLOSE) {
		ptr += 2;
		unsigned short state_code = ws_length_short(ptr);
		ws_close(client, state_code);
		return NULL;
	}

	//second byte
	ptr++;
	if(((*ptr) & 0x80) == 0x80)
		frame->bit_mask = 1;
	else {
		//TODO terminate connection
		frame->bit_mask = 0;
	}
	length = ((*ptr) & 0x7F);
	if(length > 0 && length < 126) {
		frame->length = length;
		ptr++;
	}
	else if(length == 126) {
		ptr++;
		frame->length = ws_length_short(ptr);
		ptr += 2;
	}
	else if(length == 127) {
		ptr++;
		frame->length = ws_length_u64(ptr);
		ptr += 8;
	}

	memcpy(&frame->masking_key, ptr, 4);
	ptr += 4;

	if(((buffer->raw_data + buffer->data_len) - ptr) < frame->length)
		return NULL;	//data not finish
	mask(ptr, buffer->raw_data + buffer->data_len, (unsigned char *)(&frame->masking_key));
	return ptr;
}

void chat_nick(struct client *client, char *nick) {
	struct ws_frame *frame;
	frame = &client->req->ws_data;
	struct nb_buffer buffer;
	char *ptr;

	if(frame->length > 20)
		return ws_send_frame(client, "nickname must not large than 20 bytes");

	nb_buffer_init(&buffer);

	ptr = nick;
	while(*ptr != '\0') {
		if(!isspace(*ptr))
			break;
		ptr++;
	}

	if(*ptr == '\0')
		return;

	char *escape = htmlescape(ptr);

	nb_buffer_append(&buffer, header_get(&(client->req->res_header), "nick"));
	nb_buffer_append(&buffer, " change nick ");
	nb_buffer_append(&buffer, escape);
	
	header_set(client, "nick", escape);
	free(escape);
	ws_boardcast_frame(client, buffer.raw_data);
	nb_buffer_free(&buffer);
}

void chat_list_get(struct client *client) {
	int idx;
	struct nb_buffer buffer;

	nb_buffer_init(&buffer);

	for(idx = 0; idx < MAXCLIENT; idx++)
		if(clients_set[idx] == '1') {
			nb_buffer_append(&buffer, header_get(&(clients[idx]->req->res_header), "nick"));
			nb_buffer_append(&buffer, "<br/>");
		}
	
	ws_send_frame(client, buffer.raw_data);
	nb_buffer_free(&buffer);
}

void ws_work(struct client *client) {
	if(client->state == HEADER_FIN)
		ws_handshake(client);

	if(client->state == MESSAGING) {
		char *ptr;

		ptr = ws_parse(client);
		if(ptr != NULL) {
			struct ws_frame *frame;
			struct nb_buffer *buffer;
			frame = &client->req->ws_data;
			buffer = &frame->ws_buffer;

			if(*ptr == '/' && *(ptr+1) == 'L' && *(ptr+2) == 'I' && *(ptr+3) == 'S' && *(ptr+4) == 'T')
				chat_list_get(client);
			else if(*ptr == '/' && *(ptr+1) == 'N' && *(ptr+2) == 'I' && *(ptr+3) == 'C' && *(ptr+4) == 'K')
				chat_nick(client, ptr+5);
			else {
				if(frame->length > MAXCONTENT)
					ws_send_frame(client, "Too long\n");
				else {
					struct nb_buffer buffer_tmp;

					nb_buffer_init(&buffer_tmp);
					char *escape_data = htmlescape(ptr);
					struct http_header *hheader = &(client->req->res_header);

					nb_buffer_printf(&buffer_tmp, "%s> %s", 
								     header_get(hheader, "nick"), 
								     escape_data);

					free(escape_data);
					ws_boardcast_frame(client, buffer_tmp.raw_data);

					nb_buffer_free(&buffer_tmp);
				}
			}

			nb_buffer_free(buffer);
		}
	}
}

void http_work(struct client *client) {
	if(client->state == HEADER_PARSE) {
		http_parse(client);
		if(client->state == HEADER_PARSE)
			return;
	}

	if(client->state == MESSAGING || header_equal(client, "Upgrade", "websocket") == 0)
		ws_work(client);	//websocket request;
	else
		http_return(client, START_PAGE);
}

void do_http(struct epoll_event* ev) {
	int client_socket;
	struct client* client;
	int nread, nwrite;
	int buf_len;
	char* buf_ptr;
	struct nb_buffer *buffer;

	buf_len = MAXSIZE;
	client = (struct client*) ev->data.ptr;
	client->ev = ev;
	client_socket = client->client_socket;
	
	if((ev->events & EPOLLOUT) == EPOLLOUT) {
		buffer = &client->req->res_buffer;
		nwrite = send(client_socket, buffer->raw_data, buffer->data_len, 0);
	}
	else {
		buffer = &client->req->ws_data.ws_buffer;
		//fprintf(stderr, "socket %d readable\n", client_socket);
		nread = 0;
		while(1) {
			if(client->state == HEADER_PARSE)
				buf_ptr = client_buffer_get(client, buf_len);
			else if(client->state == MESSAGING)
				buf_ptr = nb_buffer_get(buffer, buf_len);

			nread = recv(client_socket, buf_ptr, buf_len, 0);
			//fprintf(stderr, "read %d bytes, %s\n", nread, buf_ptr);
			if(nread == 0 ) {
				if(client->state == MESSAGING) {
					char present[51];
					snprintf(present, sizeof(present) - 1, "Notification> %s leave", header_get(&client->req->res_header, "nick"));
					clients_set[client->client_socket] = '\0';
					ws_boardcast_frame(client, present);
				}

				client_disconnect(client);
				return;
			}
			else if(nread < 0) {
				//fprintf(stderr, "errno %d\n", errno);
				if(errno != EAGAIN) {
					perror("recv");
					return;
				}
				else {
					//fprintf(stderr, "EAGAIN\n");
					break;
				}
			}
			else {
				if(client->state == HEADER_PARSE)
					client_buffer_add(client, nread);
				else if(client->state == MESSAGING)
					nb_buffer_add(buffer, nread);
			}

			if(nread < buf_len)
				break;
		}
		http_work(client);
	}
}

void do_echo(struct epoll_event* ev) {
	int client_socket, idx;
	char buf[MAXSIZE + 1] = {0};
	int buf_len = MAXSIZE;
	int nread;
	struct client* client_user;

	client_user = (struct client*) ev->data.ptr;
	client_socket = client_user->client_socket;
	//printf("%d\n", client_socket);

	do {
		nread = recv(client_socket, buf, buf_len, 0);
		if(nread == 0) {
			epoll_ctl(event_fd, EPOLL_CTL_DEL, client_socket, ev);
			close(client_socket);
			free_client(client_socket);
			ev->data.ptr = 0;
			break;
		}
		else if(nread < 0) {
			perror("recv");
			return;
		}
		else {
			buf[nread] = '\0';
			fprintf(stderr, "recv: %s\n", buf);
			for(idx = 0; idx < MAXCLIENT; idx++) {
				if(idx != client_socket && clients_set[idx] == '1') {
					send(idx, buf, nread, 0);
				}
			}
		}
	} while(nread == buf_len);
}

Sigfunc Signal(int signo, Sigfunc func)
{
	struct sigaction act, oact;

	act.sa_handler = func;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	if (signo == SIGALRM)
	{
#ifdef	SA_INTERRUPT
		act.sa_flags |= SA_INTERRUPT;	/* SunOS 4.x */
#endif
	}
	else
	{
#ifdef	SA_RESTART
		act.sa_flags |= SA_RESTART;		/* SVR4, 44BSD */
#endif
	}

	if(sigaction(signo, &act, &oact) < 0)
		return (SIG_ERR);
	return oact.sa_handler;
}

static int daemonize(const char* appname) {
	int i;
	pid_t pid;

	if((pid = fork())<0)
		return -1;
	else if(pid>0)
		_exit(0);

	if(setsid()<0)
		return -1;

	Signal(SIGCHLD, SIG_IGN);

	if((pid=fork())<0)
		return -1;
	else if(pid>0)
		_exit(0);

	chdir("/");

	for(i = 0; i < 64; i++)
		close(i);

	open("/dev/null", O_RDONLY);
	open("/dev/null", O_RDWR);
	open("/dev/null", O_RDWR);

	//openlog(appname, LOG_PID, LOG_USER);

	return 0;
}

void ws_signal_handler(int signo) {
}

int main(int argc, char* argv[]) {
	int error;
	char c;
	
	while(-1 != (c = getopt(argc, argv, "p:l:a:c:t:m:u:P:dhv"))) {
		switch(c) {
		case 'p':
			server_port = atoi(optarg);
			break;
		case 'l':
			server_host = strdup(optarg);
			break;
		case 'a':
			htdocs_dir = optarg;
			break;

		case 'h':
		default:
			usage();
			return 0;
		}
	}
	if(server_port == -1)
		server_port = DEF_PORT;

	/*
	if(!htdocs_dir) {
		fprintf(stderr, "-d htdocs-dir\n");
		return -1;
	}
	*/

	if(0 != daemonize("wschat"))
		return -1;

    Signal(SIGQUIT, ws_signal_handler);
    Signal(SIGTERM, ws_signal_handler);
    Signal(SIGINT, ws_signal_handler);
    Signal(SIGCHLD, ws_signal_handler);
    Signal(SIGHUP, ws_signal_handler);

	if((error = tcp_server_socket()) < 0) {
		return error;
	}
	if((event_fd = epoll_create(10)) < 0) {
		perror("epoll_create");
		return event_fd;
	}
	//printf("evfd=%d\n", event_fd);

	int idx = 0;
	int idx1 = 0;
	int ready;
	int isServerReadable = 0;
	int client_socket;
	struct sockaddr_in addr;
	size_t addr_len = sizeof(addr);
	struct epoll_event ev, ev1;

	for(idx = 0; idx < MAXCLIENT; idx++) {
		clients_set[idx] = '\0';
	}

	idx = 0;
	//add all listen socket to epoll
	while(server_sockets[idx] != -1) {
		//printf("add ev %d\n", server_sockets[idx]);
		ev.events = EPOLLIN | EPOLLET;
		ev.data.fd = server_sockets[idx];
		error = epoll_ctl(event_fd, EPOLL_CTL_ADD, server_sockets[idx], &ev);
		if(error < 0) {
			perror("epoll_ctl");
			close(event_fd);
			return -1;
		}
		idx++;
	}
	while(1) {
		ready = epoll_wait(event_fd, &ev, 1, -1);
		if(ready < 0) {
			perror("epoll_wait");
			continue;
		}
		else if(ready == 0) {
			//Timeout
			continue;
		}
		//fprintf(stderr, "ready=%d\n", ready);
		for(idx = 0; idx < ready; idx++) {
			//printf("fd = %d\n", ev.data.fd);
			idx1 = 0;
			isServerReadable = 0;
			while(server_sockets[idx1] != -1) {
				if(server_sockets[idx1] == ev.data.fd) {
					isServerReadable = 1;
					while(1) {
						client_socket = accept(ev.data.fd, (struct sockaddr *)&addr, &addr_len);
						if(client_socket < 0) {
							if(errno == EAGAIN || errno == EWOULDBLOCK)
								break;
							perror("accept");
							return -1;
						}
						if(client_socket >= MAXCLIENT) {
							fprintf(stderr, "too many connections");
							return -1;
						}
						struct client* c = new_client(client_socket);
						if( !c ) {
							fprintf(stderr, "new client failed\n");
							return -1;
						}
						//fprintf(stderr, "%d / %d, new socket %d\n", idx + 1, ready, client_socket);
						setnonblock(client_socket);
						ev1.events = EPOLLIN | EPOLLET;
						ev1.data.ptr = c;
						error = epoll_ctl(event_fd, EPOLL_CTL_ADD, client_socket, &ev1);
					}
				}
				idx1++;
			}
			if(!isServerReadable) {
				do_http(&ev);
			}
		}
	}
	close(event_fd);
	return 0;
}
