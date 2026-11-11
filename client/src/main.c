#include <stdio.h>
#include <stdlib.h>

#include "connection.h"
#include "sender.h"

#define SERVER_IP   "127.0.0.1"
#define SERVER_PORT 8080

int main(void) {

    int sockfd = connect_to_server(SERVER_IP, SERVER_PORT);

    if (sockfd < 0) {
        perror("connect_to_server");
        return EXIT_FAILURE;
    }

    printf("Connected to server!\n");

    int result = send_loop(sockfd);

    if (close_connection(sockfd) < 0)
        perror("close_connection");

    if (result < 0)
        return EXIT_FAILURE;

    printf("Client session terminated.\n");

    return EXIT_SUCCESS;
}
