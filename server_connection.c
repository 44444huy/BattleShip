#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include "server.h"

void accept_new_client(struct Server *s, int listen_fd)
{
    int fd = accept(listen_fd, NULL, NULL);
    if (fd == -1) return;
    int index = -1;
    for (int i = 0; i < MAX_CLIENTS; ++i) if (s->clients[i].fd == -1) { index = i; break; }
    if (index == -1 || fd >= FD_SETSIZE) {
        const char full[] = "ERROR Server full\n";
        (void)send(fd, full, sizeof(full)-1, MSG_NOSIGNAL | MSG_DONTWAIT);
        close(fd); return;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) { close(fd); return; }
    reset_client(&s->clients[index]); s->clients[index].fd = fd;
    emit(s, index, "CONNECTED");
}
int receive_client_data(struct Server *s, int ci)
{
    struct Client *c = &s->clients[ci];
    char bytes[RECEIVE_CHUNK_SIZE];
    ssize_t size = recv(c->fd, bytes, sizeof(bytes), 0);
    if (size == 0) return 1;
    if (size < 0) return errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK;
    for (ssize_t i = 0; i < size; ++i) {
        unsigned char ch = (unsigned char)bytes[i];
        if (c->discarding_line) { if (ch == '\n') c->discarding_line = 0; continue; }
        if (ch == '\r') continue;
        if (ch == '\n') {
            c->buffer[c->used] = '\0';
            process_message(s, ci, c->buffer);
            c->used = 0;
            if (c->closing || c->failed) break;
        } else if ((ch < 32 && ch != '\t') || ch == 127 || c->used + 1 >= sizeof(c->buffer)) {
            emit(s, ci, "ERROR Invalid or oversized message");
            c->used = 0; c->discarding_line = 1;
        } else c->buffer[c->used++] = (char)ch;
    }
    return c->failed;
}
int flush_client(struct Server *s, int ci)
{
    struct Client *c = &s->clients[ci];
    if (c->output_used) {
        ssize_t n = send(c->fd, c->output, c->output_used, MSG_NOSIGNAL);
        if (n < 0) return errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK;
        if (n == 0) return 1;
        c->output_used -= (size_t)n;
        memmove(c->output, c->output+n, c->output_used);
    }
    return c->closing && c->output_used == 0;
}
