#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include "connection.h"
#include "receiver.h"
#include "server_config.h"

static volatile sig_atomic_t shutdown_requested = 0;

static void handle_sigint(int signal_number) {
    (void)signal_number;
    shutdown_requested = 1;
}

int main(void) {

    struct sigaction sa = {0};

    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    int server_fd = start_server(SERVER_IP, SERVER_PORT);

    if (server_fd < 0) {
        perror("start_server");
        return EXIT_FAILURE;
    }

    int client_fd = -1;

    printf("Server listening on %s:%d\n",
           SERVER_IP, SERVER_PORT);

    while (!shutdown_requested) {

        printf("\nWaiting for a client...\n");

        client_fd = accept_client(server_fd);

        if (client_fd < 0) {

            if (errno == EINTR && shutdown_requested)
                break;

            perror("accept_client");
            continue;
        }

        if (shutdown_requested)
            break;

        printf("Client connected!\n");

        int result = receive_loop(client_fd);

        if (result == 0)
            printf("Client session ended normally.\n");
        else
            printf("Client session terminated due to an error.\n");

        close_connection(client_fd);
        client_fd = -1;

        printf("Client connection closed.\n");
    }

    printf("\nShutdown requested.\n");

    if (client_fd >= 0) {
        printf("Closing active client connection...\n");
        close_connection(client_fd);
    }

    printf("Closing listening socket...\n");
    stop_server(server_fd);

    printf("Server shutdown complete.\n");

    return EXIT_SUCCESS;
}
