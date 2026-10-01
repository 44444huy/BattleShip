#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    printf("  HELLO, PING, QUIT\n");

    char message[MESSAGE_SIZE];

    while (1)
    {
        printf("> ");
        fflush(stdout);

        if (fgets(message, sizeof(message), stdin) == NULL)
        {
            printf("\nInput ended.\n");
            break;
        }

        message[strcspn(message, "\r\n")] = '\0';

        if (message[0] == '\0')
        {
            continue;
        }

        if (send_line(server_fd, message) == -1)
        {
            perror("send");
            break;
        }

        receive_result = receive_line(server_fd,
                                      response, sizeof(response));

        if (receive_result == 1)
        {
            printf("Server replied: %s\n", response);
        }
        else if (receive_result == 0)
        {
            printf("Server closed the connection.\n");
            break;
        }
        else
        {
            fprintf(stderr, "Could not receive a complete response.\n");
            break;
        }

        if (strcmp(message, "QUIT") == 0)
        {
            break;
        }
    }

    close(server_fd);
    return 0;
}
