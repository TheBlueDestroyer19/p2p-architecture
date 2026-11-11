#ifndef CONNECTION_H
#define CONNECTION_H

int connect_to_server(const char *ip, unsigned short port);

int close_connection(int sockfd);

#endif
