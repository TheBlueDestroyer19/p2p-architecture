#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "connection.h"
#include "server_config.h"

int start_server(const char *ip, unsigned short port) {

    int server_fd;
    struct sockaddr_in address;

    // Create TCP socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
        return -1;

    // Enable address reuse
    int opt = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0) {
        close(server_fd);
        return -1;
    }

    // Configure server address
    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (inet_pton(AF_INET, ip, &address.sin_addr) <= 0) {
        close(server_fd);
        errno = EINVAL;
        return -1;
    }

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        close(server_fd);
        return -1;
    }

    // Start listening
    if (listen(server_fd, SERVER_BACKLOG) < 0) {
        close(server_fd);
        return -1;
    }

    return server_fd;
}


int accept_client(int server_fd) {
  return accept(server_fd,NULL,NULL);
}


int close_connection(int sockfd) {

    return close(sockfd);
}


int stop_server(int server_fd) {

    return close(server_fd);
}
