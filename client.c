#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "protocol.h"

#define MESSAGE_SIZE 256

static int parse_port(const char *text, int *port)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || text[0] == '\0' || end[0] != '\0' ||
        value < 1 || value > 65535)
    {
        return 0;
    }

    *port = (int)value;
    return 1;
}

int main(int argc, char *argv[])
{
    int port;

    if (argc != 3 || !parse_port(argv[2], &port))
    {
        fprintf(stderr, "Usage: %s <server IP> <port 1-65535>\n", argv[0]);
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_address = {0};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons((unsigned short)port);

    if (inet_pton(AF_INET, argv[1], &server_address.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid IPv4 address.\n");
        close(server_fd);
        return 1;
    }

    if (connect(server_fd, (struct sockaddr *)&server_address,
                sizeof(server_address)) == -1)
    {
        perror("connect");
        close(server_fd);
        return 1;
    }

    printf("Connected to the server.\n");

    char response[MESSAGE_SIZE];
    int receive_result = receive_line(server_fd,
                                      response, sizeof(response));

    if (receive_result != 1)
    {
        fprintf(stderr, "Connection closed during setup.\n");
        close(server_fd);
        return 1;
    }

    printf("Server: %s\n", response);

    printf("Commands:\n");
    printf("  REGISTER <username> <password>\n");
    printf("  LOGIN <username> <password>\n");
    printf("  LOGOUT\n");
    printf("  READY, UNREADY, LIST_READY, LIST_ONLINE\n");
    printf("  CHALLENGE <username>\n");
    printf("  LIST_CHALLENGES\n");
    printf("  ACCEPT <username>, DECLINE <username>\n");
    printf("  PLACE <ship 1-5> <A1-J10> <H|V>\n");
    printf("  SHOW_BOARD\n");
    printf("  SHOOT <A1-J10>\n");
    printf("  SHOW_OPPONENT\n");
    printf("  PAUSE, RESUME, RESIGN\n");
    printf("  DRAW, ACCEPT_DRAW, DECLINE_DRAW\n");
    printf("  REMATCH, ACCEPT_REMATCH, DECLINE_REMATCH\n");
    printf("  HELLO, PING, QUIT\n");

    char message[MESSAGE_SIZE];
    int input_open = 1;
    int quit_sent = 0;
    int show_prompt = 1;

    setvbuf(stdin, NULL, _IONBF, 0);

    while (1)
    {
        if (show_prompt && input_open && !quit_sent)
        {
            printf("> ");
            fflush(stdout);
            show_prompt = 0;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        if (input_open && !quit_sent)
        {
            FD_SET(STDIN_FILENO, &read_fds);
        }

        int ready = select(server_fd + 1, &read_fds,
                           NULL, NULL, NULL);

        if (ready == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("select");
            break;
        }

        if (FD_ISSET(server_fd, &read_fds))
        {
            receive_result = receive_line(server_fd,
                                          response, sizeof(response));

            if (receive_result == 1)
            {
                printf("\nServer: %s\n", response);

                if (strcmp(response, "BYE") == 0)
                {
                    break;
                }

                show_prompt = 1;
            }
            else if (receive_result == 0)
            {
                printf("\nServer closed the connection.\n");
                break;
            }
            else
            {
                fprintf(stderr,
                        "\nCould not receive a complete response.\n");
                break;
            }
        }

        if (!input_open || quit_sent ||
            !FD_ISSET(STDIN_FILENO, &read_fds))
        {
            continue;
        }

        if (fgets(message, sizeof(message), stdin) == NULL)
        {
            input_open = 0;
            shutdown(server_fd, SHUT_WR);
            printf("\nInput ended. Waiting for server...\n");
            continue;
        }

        message[strcspn(message, "\r\n")] = '\0';

        if (message[0] == '\0')
        {
            show_prompt = 1;
            continue;
        }

        if (send_line(server_fd, message) == -1)
        {
            perror("send");
            break;
        }

        if (strcmp(message, "QUIT") == 0)
        {
            quit_sent = 1;
        }

        show_prompt = 1;
    }

    close(server_fd);
    return 0;
}
