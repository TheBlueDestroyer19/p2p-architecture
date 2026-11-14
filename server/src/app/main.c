#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "connection.h"
#include "connection_manager.h"
#include "server_config.h"

/*
 * The signal handler only sets this flag.
 * The connection manager observes it and initiates
 * graceful shutdown.
 */
static volatile sig_atomic_t shutdown_requested = 0;

static void handle_sigint(int signal_number)
{
    (void)signal_number;
    shutdown_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction sa = {0};

    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);

    /*
     * Do not use SA_RESTART.
     * We want SIGINT to interrupt poll() so that
     * the main connection manager can detect shutdown.
     */
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    return 0;
}

int main(void)
{
    int server_fd = -1;
    int manager_status = EXIT_FAILURE;

    if (install_signal_handlers() == -1) {
        return EXIT_FAILURE;
    }

    /*
     * Initialize connection manager resources:
     * connection table, mutex, notification pipe.
     */
    if (connection_manager_init() != 0) {
        fprintf(stderr, "Failed to initialize connection manager.\n");
        return EXIT_FAILURE;
    }

    /*
     * Create and start the listening socket.
     */
    server_fd = start_server(SERVER_IP, SERVER_PORT);

    if (server_fd == -1) {
        fprintf(stderr, "Failed to start server.\n");
        connection_manager_cleanup();
        return EXIT_FAILURE;
    }

    printf("Server listening on %s:%d\n",
           SERVER_IP, SERVER_PORT);

    /*
     * The connection manager handles:
     * - poll()
     * - accepting clients
     * - worker creation
     * - worker completion
     * - graceful shutdown
     *
     * The listening socket remains owned by main().
     */
    manager_status =
        connection_manager_run(server_fd, &shutdown_requested);

    /*
     * All workers must have exited and been joined
     * before manager resources are destroyed.
     */
    connection_manager_cleanup();

    /*
     * Main owns the listening socket.
     */
    if (stop_server(server_fd) == -1) {
        perror("stop_server");
        manager_status = EXIT_FAILURE;
    }

    printf("Server shutdown.\n");

    return manager_status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

