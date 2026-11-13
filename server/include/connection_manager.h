
#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <signal.h>

typedef enum {
    CLIENT_IDLE,
    CLIENT_TRANSFERRING
} ClientActivity;

int connection_manager_init(void);

int connection_manager_run(
    int server_fd,
    volatile sig_atomic_t *shutdown_requested
);

void connection_manager_cleanup(void);

int connection_manager_set_activity(
    int slot_id,
    ClientActivity activity
);

int connection_manager_try_start_transfer(int slot_id);

int connection_manager_is_shutting_down(void);

#endif

