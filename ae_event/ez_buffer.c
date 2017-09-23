/*
*	for nonblock opreation
*/


#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>

#include "ez_buffer.h"
#include "debug.h"

static const size_t init_buffer_size = (2 * 1024);
static const size_t shrink_buffer_size = (64 * 1024 * 1024);

bool ez_buffer_init(ez_buffer *ez_buffer)
{
    	ez_buffer->buffer_base = malloc(init_buffer_size);
	if (!ez_buffer->buffer_base)
		return false;
    	ez_buffer->buffer_size = init_buffer_size;
    	ez_buffer->read_index = ez_buffer->write_index = 0;
	return true;
}

void ez_buffer_free(ez_buffer *ez_buffer)
{
	if (!ez_buffer)
		return;
	free(ez_buffer->buffer_base);
	ez_buffer->buffer_base = NULL;
}

void reset_buffer(ez_buffer *ez_buffer)
{
        ez_buffer->read_index = ez_buffer->write_index = 0;
}

bool append_buffer(ez_buffer *ez_buffer, const char *data, size_t length)
{
    	if (!data || !reserve_space(ez_buffer, length)) {
		return false;
    	}
    	memcpy(ez_buffer->buffer_base + ez_buffer->write_index, data, length);
    	ez_buffer->write_index += length;
    	return true;
}

bool append_buffer_ex(ez_buffer *ez_buffer, size_t length)
{
    	if (length > ez_buffer->buffer_size - ez_buffer->write_index) {
		return false;
    	}
    	ez_buffer->write_index += length;
    	return true;
}

bool erase_buffer(ez_buffer *ez_buffer, size_t length)
{
    	if (ez_buffer->write_index - ez_buffer->read_index < length) {
		return false;
    	}
    	ez_buffer->read_index += length;

    	if (ez_buffer->buffer_size >= shrink_buffer_size
		&& ez_buffer->write_index - ez_buffer->read_index < init_buffer_size) {
		char *smaller_buffer = malloc(init_buffer_size);
		if (!smaller_buffer)
			return true;
		memcpy(smaller_buffer, ez_buffer->buffer_base + ez_buffer->read_index,
	       		ez_buffer->write_index - ez_buffer->read_index);
		free(ez_buffer->buffer_base);

		ez_buffer->buffer_base = smaller_buffer;
		ez_buffer->buffer_size = init_buffer_size;
		ez_buffer->write_index -= ez_buffer->read_index;
		ez_buffer->read_index = 0;
    	}
    	return true;
}

bool reserve_space(ez_buffer *ez_buffer, size_t length)
{
    	if (ez_buffer->buffer_size - ez_buffer->write_index >= length) {

    	} else if (ez_buffer->buffer_size - ez_buffer->write_index +
			 ez_buffer->read_index >= length) {
		memmove(ez_buffer->buffer_base, ez_buffer->buffer_base + ez_buffer->read_index,
			ez_buffer->write_index - ez_buffer->read_index);
		ez_buffer->write_index -= ez_buffer->read_index;
		ez_buffer->read_index = 0;
    	} else {
		size_t new_buffer_size = ez_buffer->write_index - ez_buffer->read_index + length;

		char *new_buffer = malloc(new_buffer_size);
		if (!new_buffer)
			return false;
		memcpy(new_buffer, ez_buffer->buffer_base + ez_buffer->read_index,
				ez_buffer->write_index - ez_buffer->read_index);
		free(ez_buffer->buffer_base);

		ez_buffer->buffer_base = new_buffer;
		ez_buffer->buffer_size = new_buffer_size;
		ez_buffer->write_index -= ez_buffer->read_index;
		ez_buffer->read_index = 0;
    	}
    	return true;
}

void get_buffer_begin(ez_buffer *ez_buffer, const char **buffer, size_t * length)
{
    	if (buffer) {
		*buffer = ez_buffer->buffer_base + ez_buffer->read_index;
    	}
    	if (length) {
		*length = ez_buffer->write_index - ez_buffer->read_index;
    	}
}

size_t get_buffer_length(ez_buffer *ez_buffer)
{
    	return ez_buffer->write_index - ez_buffer->read_index;
}

void get_space_begin(ez_buffer *ez_buffer, char **buffer, size_t *length)
{
    	if (buffer)
		*buffer = ez_buffer->buffer_base + ez_buffer->write_index;
    	if (length)
		*length = ez_buffer->buffer_size - ez_buffer->write_index;
}

