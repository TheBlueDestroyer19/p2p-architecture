
#define _GNU_SOURCE

#include "connection_manager.h"
#include "connection.h"
#include "receiver.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <limits.h>

#define MAX_CLIENTS 5
#define SHUTDOWN_GRACE_PERIOD_SECONDS 60
#define NORMAL_POLL_INTERVAL_MS 1000
#define FORCED_POLL_INTERVAL_MS 1000

/* -------------------------------------------------- */
/* DATA STRUCTURES                                    */
/* -------------------------------------------------- */

typedef enum {
    SLOT_FREE,
    SLOT_RUNNING,
    SLOT_FINISHED
} ClientSlotState;

typedef struct {
    int client_fd;
    pthread_t thread_id;
    ClientSlotState state;
    ClientActivity activity;
    struct ClientContext *context;
} ClientConnection;

typedef struct ClientContext {
    int client_fd;
    int slot_id;
} ClientContext;


/* -------------------------------------------------- */
/* SHARED STATE                                       */
/* -------------------------------------------------- */

static ClientConnection clients[MAX_CLIENTS];

static pthread_mutex_t clients_mutex;
static int completion_pipe[2] = {-1, -1};

static int mutex_initialized = 0;
static int manager_initialized = 0;
static int shutting_down = 0;


/* -------------------------------------------------- */
/* INITIALIZATION                                     */
/* -------------------------------------------------- */

static void initialize_connection_table(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].client_fd = -1;
        clients[i].state = SLOT_FREE;
        clients[i].activity = CLIENT_IDLE;
        clients[i].context = NULL;
    }
}


static int configure_pipe_fd(int fd)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags == -1 ||
        fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    flags = fcntl(fd, F_GETFD);

    if (flags == -1 ||
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        return -1;
    }

    return 0;
}


int connection_manager_init(void)
{
    if (manager_initialized) {
        fprintf(stderr,
                "Connection manager already initialized.\n");
        return -1;
    }

    initialize_connection_table();

    int ret = pthread_mutex_init(&clients_mutex, NULL);

    if (ret != 0) {
        fprintf(stderr,
                "pthread_mutex_init failed: %d\n", ret);
        return -1;
    }

    mutex_initialized = 1;

    if (pipe(completion_pipe) == -1) {
        perror("pipe");
        connection_manager_cleanup();
        return -1;
    }

    if (configure_pipe_fd(completion_pipe[0]) == -1 ||
        configure_pipe_fd(completion_pipe[1]) == -1) {

        perror("fcntl");
        connection_manager_cleanup();
        return -1;
    }

    shutting_down = 0;
    manager_initialized = 1;

    printf("Connection manager initialized.\n");

    return 0;
}


/* -------------------------------------------------- */
/* WORKER THREAD                                      */
/* -------------------------------------------------- */

static void *client_worker(void *arg)
{
    ClientContext *context = arg;

    /* Copy values immediately. */
    int client_fd = context->client_fd;
    int slot_id = context->slot_id;

    printf("[Worker %d] Client connected (fd=%d).\n",
           slot_id, client_fd);

    /*
     * The worker owns the accepted socket.
     * It handles the complete connection lifetime.
     */
    int receive_status = receive_loop(client_fd, slot_id);

    if (receive_status < 0) {
        fprintf(stderr,
                "[Worker %d] Client session ended with an error.\n",
                slot_id);
    }

    /*
     * Mark the slot as finished.
     *
     * Once marked FINISHED, the main thread will no longer
     * perform socket operations on this descriptor.
     */
    pthread_mutex_lock(&clients_mutex);

    clients[slot_id].state = SLOT_FINISHED;
    clients[slot_id].activity = CLIENT_IDLE;

    pthread_mutex_unlock(&clients_mutex);

    /*
     * Worker retains ownership of closing its socket.
     */
    if (close_connection(client_fd) < 0) {
        perror("close_connection");
    }

    /*
     * Notify the main thread.
     *
     * EAGAIN is safe because a full pipe is already readable.
     */
    char notification = 1;

    for (;;) {

        ssize_t written =
            write(completion_pipe[1], &notification, 1);

        if (written == 1) {
            break;
        }

        if (written == -1 && errno == EINTR) {
            continue;
        }

        if (written == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }

        perror("Worker notification");
        break;
    }

    printf("[Worker %d] Finished.\n", slot_id);

    return NULL;
}


/* -------------------------------------------------- */
/* CONNECTION ADMISSION                               */
/* -------------------------------------------------- */

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].state == SLOT_FREE) {
            return i;
        }
    }

    return -1;
}


static int admit_client(int client_fd)
{
    pthread_mutex_lock(&clients_mutex);

    /*
     * Never admit clients after shutdown begins.
     */
    if (shutting_down) {
        pthread_mutex_unlock(&clients_mutex);

        close_connection(client_fd);
        return -1;
    }

    int slot_id = find_free_slot();

    if (slot_id == -1) {
        pthread_mutex_unlock(&clients_mutex);

        fprintf(stderr,
                "Maximum client capacity reached.\n");

        close_connection(client_fd);
        return -1;
    }

    ClientContext *context = malloc(sizeof(*context));

    if (context == NULL) {
        pthread_mutex_unlock(&clients_mutex);

        perror("malloc");
        close_connection(client_fd);

        return -1;
    }

    context->client_fd = client_fd;
    context->slot_id = slot_id;

    /*
     * Reserve the slot before creating the worker.
     * The worker cannot update it until this mutex
     * is released.
     */
    clients[slot_id].client_fd = client_fd;
    clients[slot_id].state = SLOT_RUNNING;
    clients[slot_id].activity = CLIENT_IDLE;
    clients[slot_id].context = context;

    int ret = pthread_create(
        &clients[slot_id].thread_id,
        NULL,
        client_worker,
        context
    );

    if (ret != 0) {

        fprintf(stderr,
                "pthread_create failed: %d\n", ret);

        clients[slot_id].client_fd = -1;
        clients[slot_id].state = SLOT_FREE;
        clients[slot_id].activity = CLIENT_IDLE;
        clients[slot_id].context = NULL;

        pthread_mutex_unlock(&clients_mutex);

        free(context);
        close_connection(client_fd);

        return -1;
    }

    printf("Client assigned to slot %d.\n", slot_id);

    pthread_mutex_unlock(&clients_mutex);

    return 0;
}


/* -------------------------------------------------- */
/* WORKER RECLAMATION                                 */
/* -------------------------------------------------- */

static void drain_completion_pipe(void)
{
    char buffer[128];

    for (;;) {

        ssize_t n = read(
            completion_pipe[0],
            buffer,
            sizeof(buffer)
        );

        if (n > 0) {
            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        if (n == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }

        if (n == 0) {
            break;
        }

        perror("Completion pipe read");
        break;
    }
}


static void reclaim_finished_workers(void)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {

        pthread_t thread_id;
        ClientContext *context = NULL;
        int finished = 0;

        pthread_mutex_lock(&clients_mutex);

        if (clients[i].state == SLOT_FINISHED) {
            thread_id = clients[i].thread_id;
            context = clients[i].context;
            finished = 1;
        }

        pthread_mutex_unlock(&clients_mutex);

        if (!finished) {
            continue;
        }

        /*
         * Join outside the mutex.
         */
        int ret = pthread_join(thread_id, NULL);

        if (ret != 0) {
            fprintf(stderr,
                    "pthread_join failed: %d\n", ret);
            continue;
        }

        free(context);

        pthread_mutex_lock(&clients_mutex);

        clients[i].client_fd = -1;
        clients[i].state = SLOT_FREE;
        clients[i].activity = CLIENT_IDLE;
        clients[i].context = NULL;

        pthread_mutex_unlock(&clients_mutex);

        printf("Reclaimed connection slot %d.\n", i);
    }
}


/* -------------------------------------------------- */
/* TRANSFER ACTIVITY                                  */
/* -------------------------------------------------- */

/*
 * Atomically reserve a connection for a new transfer.
 *
 * Returns:
 *   0  -> reservation successful
 *   1  -> server is shutting down
 *  -1  -> invalid slot or invalid connection state
 */
int connection_manager_try_start_transfer(int slot_id)
{
    if (slot_id < 0 || slot_id >= MAX_CLIENTS) {
        return -1;
    }

    pthread_mutex_lock(&clients_mutex);

    if (shutting_down) {
        pthread_mutex_unlock(&clients_mutex);
        return 1;
    }

    if (clients[slot_id].state != SLOT_RUNNING ||
        clients[slot_id].activity != CLIENT_IDLE) {

        pthread_mutex_unlock(&clients_mutex);
        return -1;
    }

    clients[slot_id].activity = CLIENT_TRANSFERRING;

    pthread_mutex_unlock(&clients_mutex);

    return 0;
}


/*
 * Update the activity state of a connected client.
 */
int connection_manager_set_activity(
    int slot_id,
    ClientActivity activity
)
{
    if (slot_id < 0 || slot_id >= MAX_CLIENTS) {
        return -1;
    }

    if (activity != CLIENT_IDLE &&
        activity != CLIENT_TRANSFERRING) {
        return -1;
    }

    pthread_mutex_lock(&clients_mutex);

    if (clients[slot_id].state != SLOT_RUNNING) {
        pthread_mutex_unlock(&clients_mutex);
        return -1;
    }

    clients[slot_id].activity = activity;

    pthread_mutex_unlock(&clients_mutex);

    return 0;
}


/*
 * Check whether server shutdown has started.
 */
int connection_manager_is_shutting_down(void)
{
    pthread_mutex_lock(&clients_mutex);

    int result = shutting_down;

    pthread_mutex_unlock(&clients_mutex);

    return result;
}


/* -------------------------------------------------- */
/* SHUTDOWN HELPERS                                   */
/* -------------------------------------------------- */

/*
 * Immediately disconnect idle clients.
 *
 * Called only after shutting_down has been set.
 */
static void shutdown_idle_clients(void)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (clients[i].state == SLOT_RUNNING &&
            clients[i].activity == CLIENT_IDLE) {

            printf("[Shutdown] Disconnecting idle client "
                   "in slot %d.\n", i);

            if (shutdown(clients[i].client_fd, SHUT_RDWR) < 0) {
                perror("shutdown idle client");
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}


/*
 * Forcefully disconnect all remaining running clients.
 *
 * Used when the grace period expires.
 */
static void shutdown_all_clients(void)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (clients[i].state == SLOT_RUNNING) {

            printf("[Shutdown] Forcing disconnection of client "
                   "in slot %d.\n", i);

            if (shutdown(clients[i].client_fd, SHUT_RDWR) < 0) {
                perror("shutdown client");
            }
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}


/*
 * Calculate the remaining grace-period time in milliseconds.
 *
 * Returns:
 *   0  -> success
 *  -1  -> clock_gettime failure
 */
static int remaining_timeout_ms(
    const struct timespec *deadline,
    int *timeout_ms
)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return -1;
    }

    time_t seconds = deadline->tv_sec - now.tv_sec;
    long nanoseconds = deadline->tv_nsec - now.tv_nsec;

    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }

    if (seconds < 0) {
        *timeout_ms = 0;
        return 0;
    }

    long long milliseconds =
        (long long)seconds * 1000LL +
        (nanoseconds + 999999L) / 1000000L;

    if (milliseconds > INT_MAX) {
        milliseconds = INT_MAX;
    }

    *timeout_ms = (int)milliseconds;

    return 0;
}


/*
 * Count slots that have not yet been reclaimed.
 */
static int count_active_clients(void)
{
    int active = 0;

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (clients[i].state != SLOT_FREE) {
            active++;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    return active;
}


/* -------------------------------------------------- */
/* MAIN EVENT LOOP                                    */
/* -------------------------------------------------- */

int connection_manager_run(
    int server_fd,
    volatile sig_atomic_t *shutdown_requested
)
{
    if (!manager_initialized || shutdown_requested == NULL) {
        return -1;
    }

    int run_status = 0;

    struct pollfd fds[2];

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    fds[1].fd = completion_pipe[0];
    fds[1].events = POLLIN;

    /*
     * NORMAL OPERATION
     *
     * A finite timeout ensures we periodically check
     * shutdown_requested, even if SIGINT arrives just
     * before poll() begins.
     */
    while (!*shutdown_requested) {

        int ret = poll(
            fds,
            2,
            NORMAL_POLL_INTERVAL_MS
        );

        if (ret < 0) {

            if (errno == EINTR) {
                if (*shutdown_requested) {
                    break;
                }

                continue;
            }

            perror("poll");
            run_status = -1;
            break;
        }

        /*
         * Do not admit new clients after SIGINT.
         */
        if (*shutdown_requested) {
            break;
        }

        if (ret == 0) {
            continue;
        }

        /*
         * Handle unexpected listener errors.
         */
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {

            fprintf(stderr,
                    "Listening socket poll error.\n");

            run_status = -1;
            break;
        }

        /*
         * Handle new incoming clients.
         */
        if (fds[0].revents & POLLIN) {

            if (*shutdown_requested) {
                break;
            }

            int client_fd = accept_client(server_fd);

            if (client_fd >= 0) {

                if (*shutdown_requested) {
                    close_connection(client_fd);
                } else {
                    admit_client(client_fd);
                }
            }
            else if (errno != EINTR) {
                perror("accept");
            }
        }

        /*
         * Reclaim completed workers.
         */
        if (fds[1].revents & POLLIN) {

            drain_completion_pipe();
            reclaim_finished_workers();
        }

        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {

            fprintf(stderr,
                    "Completion pipe poll error.\n");

            run_status = -1;
            break;
        }
    }


    /* ---------------------------------------------- */
    /* GRACEFUL SHUTDOWN                               */
    /* ---------------------------------------------- */

    printf("\n[Shutdown] Graceful shutdown initiated.\n");

    struct timespec deadline;

    int forced_shutdown = 0;

    /*
     * Establish the shutdown deadline.
     */
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0) {

        perror("clock_gettime");

        run_status = -1;
        forced_shutdown = 1;
    }
    else {
        deadline.tv_sec += SHUTDOWN_GRACE_PERIOD_SECONDS;
    }

    /*
     * Set shutdown state.
     *
     * This prevents any new transfer reservation.
     */
    pthread_mutex_lock(&clients_mutex);

    shutting_down = 1;

    pthread_mutex_unlock(&clients_mutex);

    /*
     * Disconnect idle clients immediately.
     */
    shutdown_idle_clients();

    /*
     * If deadline initialization failed, force shutdown.
     */
    if (forced_shutdown) {
        shutdown_all_clients();
    }

    /*
     * GRACEFUL WAIT
     *
     * Active transfers are allowed to finish.
     * Completed workers are joined by the main thread.
     */
    while (1) {

        /*
         * Process completion notifications.
         */
        drain_completion_pipe();
        reclaim_finished_workers();

        /*
         * Check whether every worker has exited.
         */
        if (count_active_clients() == 0) {
            break;
        }

        int timeout = FORCED_POLL_INTERVAL_MS;

        /*
         * Calculate remaining grace-period time.
         */
        if (!forced_shutdown) {

            if (remaining_timeout_ms(
                    &deadline,
                    &timeout
                ) < 0) {

                perror("clock_gettime");

                run_status = -1;
                timeout = 0;
            }

            /*
             * Grace period expired.
             */
            if (timeout == 0) {

                printf("[Shutdown] Grace period expired.\n");

                shutdown_all_clients();

                forced_shutdown = 1;

                continue;
            }
        }

        /*
         * Wait for worker completion notifications.
         */
        struct pollfd pipe_fd = {
            .fd = completion_pipe[0],
            .events = POLLIN
        };

        int ret = poll(&pipe_fd, 1, timeout);

        if (ret < 0) {

            if (errno == EINTR) {
                continue;
            }

            perror("shutdown poll");

            run_status = -1;

            /*
             * If polling fails, do not leave active
             * transfers running indefinitely.
             */
            if (!forced_shutdown) {
                shutdown_all_clients();
                forced_shutdown = 1;
            }

            /*
             * Avoid a busy loop if polling repeatedly fails.
             * Periodically retry worker reclamation.
             */
            struct timespec pause_time = {
                .tv_sec = 0,
                .tv_nsec = 100000000L
            };

            nanosleep(&pause_time, NULL);
        }
    }

    printf("[Shutdown] All workers have exited.\n");

    return run_status;
}


/* -------------------------------------------------- */
/* CLEANUP                                            */
/* -------------------------------------------------- */

void connection_manager_cleanup(void)
{
    if (completion_pipe[0] != -1) {

        close(completion_pipe[0]);
        completion_pipe[0] = -1;
    }

    if (completion_pipe[1] != -1) {

        close(completion_pipe[1]);
        completion_pipe[1] = -1;
    }

    if (mutex_initialized) {

        pthread_mutex_destroy(&clients_mutex);
        mutex_initialized = 0;
    }

    manager_initialized = 0;
    shutting_down = 0;

    printf("Connection manager resources released.\n");
}

