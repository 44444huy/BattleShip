#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

#define RECEIVE_LINE_ERROR -1
#define RECEIVE_LINE_TOO_LONG -2

int send_all(int fd, const char *data, size_t length);
int send_line(int fd, const char *message);
int receive_line(int fd, char *buffer, size_t buffer_size);

#endif
