#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "server.h"

static volatile sig_atomic_t stopped;
static void stop_server(int signal_number) { (void)signal_number; stopped = 1; }
int main(int argc, char **argv)
{
    char *end;
    long port = argc >= 2 ? strtol(argv[1], &end, 10) : 0;
    if ((argc != 2 && argc != 3) || !port || port < 1 || port > 65535 || *end) {
        fprintf(stderr, "Usage: %s <port 1-65535> [database path]\n", argv[0]); return 1;
    }
    const char *path = argc == 3 ? argv[2] : "battleship.db";
    char lock_path[4096];
    if (snprintf(lock_path, sizeof(lock_path), "%s.lock", path) >= (int)sizeof(lock_path)) return 1;
    int lock_fd = open(lock_path, O_CREAT | O_RDWR, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        fprintf(stderr, "Cannot lock database; another server may be running.\n");
        if (lock_fd >= 0) close(lock_fd);
        return 1;
    }
    sqlite3 *db = NULL;
    if (!storage_open(&db, path)) { fprintf(stderr, "Cannot open database.\n"); sqlite3_close(db); close(lock_fd); return 1; }
    int listener = socket(AF_INET, SOCK_STREAM, 0), reuse = 1;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_port = htons((unsigned short)port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (listener < 0 || listener >= FD_SETSIZE ||
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(listener, 32) < 0 ||
        fcntl(listener, F_SETFL, O_NONBLOCK) < 0) {
        perror("listen setup"); if (listener >= 0) close(listener);
        sqlite3_close(db); close(lock_fd); return 1;
    }
    struct Server *server = malloc(sizeof(*server));
    if (!server) { close(listener); sqlite3_close(db); close(lock_fd); return 1; }
    initialize_server(server, db);
    struct sigaction action = {0};
    action.sa_handler = stop_server; sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL); sigaction(SIGTERM, &action, NULL);
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("Server is listening on port %ld (database %s)...\n", port, path);
    int status = 0;
    while (!stopped) {
        process_deadlines(server);
        run_matchmaking(server);
        fd_set reads, writes; FD_ZERO(&reads); FD_ZERO(&writes); FD_SET(listener, &reads);
        int max_fd = listener;
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            struct Client *c = &server->clients[i];
            if (c->fd == -1) continue;
            if (c->failed || (c->closing && c->output_used == 0)) { remove_client(server, i); continue; }
            if (!c->closing) FD_SET(c->fd, &reads);
            if (c->output_used) FD_SET(c->fd, &writes);
            if (c->fd > max_fd) max_fd = c->fd;
        }
        int64_t wait = next_wakeup_ms(server);
        /* Also bounds the signal-before-select race and services queue widening. */
        if (wait < 0 || wait > 1000) wait = 1000;
        struct timeval timeout = {.tv_sec = wait/1000, .tv_usec = (wait%1000)*1000};
        int ready = select(max_fd+1, &reads, &writes, NULL, &timeout);
        if (ready < 0) { if (errno == EINTR) continue; perror("select"); status = 1; break; }
        if (stopped) break;
        process_deadlines(server);
        if (FD_ISSET(listener, &reads)) accept_new_client(server, listener);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            struct Client *c = &server->clients[i];
            if (c->fd == -1) continue;
            if (c->failed || (FD_ISSET(c->fd, &reads) && receive_client_data(server, i))) { remove_client(server, i); continue; }
            if (FD_ISSET(c->fd, &writes) && flush_client(server, i)) remove_client(server, i);
        }
    }
    server->shutting_down = 1;
    for (int i = 0; i < MAX_CLIENTS; ++i) remove_client(server, i);
    free(server); close(listener); sqlite3_close(db); close(lock_fd);
    return status;
}
