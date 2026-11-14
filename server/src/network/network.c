
#include <sys/socket.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>

ssize_t recv_all(int sockfd, void *buffer, size_t length) {
    char *ptr = (char *)buffer;
    size_t total_received = 0;

    while (total_received < length) {
        ssize_t n = recv(sockfd,
                         ptr + total_received,
                         length - total_received,
                         0);

        if (n == 0)
            break;

        if (n < 0) {
            //if (errno == EINTR) continue;

            return -1;
        }

        total_received += (size_t)n;
    }

    return (ssize_t)total_received;
}

int send_all(int sockfd, const void *buffer, size_t length) {
    const char *ptr = (const char *)buffer;
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t n = send(sockfd, ptr + total_sent, length - total_sent, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            return -1;
        }

        if (n == 0)
            return -1;

        total_sent += (size_t)n;
    }

    return 0;
}
