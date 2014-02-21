#include "core.h"

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

