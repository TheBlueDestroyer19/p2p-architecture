#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "connection.h"

int connect_to_server(const char *ip, unsigned short port) {

    int sockfd;
    struct sockaddr_in server_address;

    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
        return -1;

    // Configure server address
    memset(&server_address, 0, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);

    if (inet_pton(AF_INET, ip,
                  &server_address.sin_addr) <= 0) {
        close(sockfd);
        errno = EINVAL;
        return -1;
    }

    // Establish connection
    if (connect(sockfd, (struct sockaddr *)&server_address,
                sizeof(server_address)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}


int close_connection(int sockfd) {

    return close(sockfd);
}
