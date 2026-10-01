#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "protocol.h"

int send_all(int fd, const char *data, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(fd, data + total_sent,
                            length - total_sent, MSG_NOSIGNAL);

        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (sent == 0)
        {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

int send_line(int fd, const char *message)
{
    if (message == NULL)
    {
        return -1;
    }

    if (send_all(fd, message, strlen(message)) == -1)
    {
        return -1;
    }

    return send_all(fd, "\n", 1);
}

int receive_line(int fd, char *buffer, size_t buffer_size)
{
    size_t used = 0;

    if (buffer == NULL || buffer_size == 0)
    {
        return RECEIVE_LINE_ERROR;
    }

    while (1)
    {
        char character;
        ssize_t received = recv(fd, &character, 1, 0);

        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return RECEIVE_LINE_ERROR;
        }

        if (received == 0)
        {
            return 0;
        }

        if (character == '\n')
        {
            buffer[used] = '\0';
            return 1;
        }

        if (character == '\r')
        {
            continue;
        }

        if (used + 1 >= buffer_size)
        {
            do
            {
                received = recv(fd, &character, 1, 0);
            } while (received > 0 && character != '\n');

            buffer[0] = '\0';
            return RECEIVE_LINE_TOO_LONG;
        }

        buffer[used] = character;
        used++;
    }
}
