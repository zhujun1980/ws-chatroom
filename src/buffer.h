#ifndef WSCHAT_BUFFER_H
#define WSCHAT_BUFFER_H

struct nb_buffer {
	char *raw_data;
	int data_len;
	int buf_len;
};

void nb_buffer_init(struct nb_buffer *buffer);

void nb_buffer_alloc(struct nb_buffer *buffer, int size);

void nb_buffer_free(struct nb_buffer *buffer);

void nb_buffer_clean(struct nb_buffer *buffer);

void nb_buffer_memcpy(struct nb_buffer *buffer,  void *byte, size_t length);

void nb_buffer_realloc(struct nb_buffer *buffer, int new_size);

int nb_buffer_append(struct nb_buffer *buffer, char *data);

int nb_buffer_printf(struct nb_buffer *buffer, const char *fmt, ...);

#endif

