#ifndef NETWORK_H 
#define NETWORK_H

#include <stddef.h>
#include <sys/types.h>

int send_all(int sockfd, const void *buffer, size_t length);
ssize_t recv_all(int sockfd, void *buffer, size_t length);

#endif // NETWORK.18:27:47
