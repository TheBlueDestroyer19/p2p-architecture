#ifndef CONNECTION_H
#define CONNECTION_H

int start_server(const char *ip, unsigned short port);

int accept_client(int server_fd);

int close_connection(int sockfd);

int stop_server(int server_fd);

#endif
