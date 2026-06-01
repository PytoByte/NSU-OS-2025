#pragma once

#include <poll.h>
#include "buffer.h"
#include "cache.h"

#define MAX_CONNS 256
#define BUF_CHUNK 4096

typedef enum {
    ST_FREE,
    ST_READ_REQ,
    ST_CONNECTING,
    ST_SEND_REQ,
    ST_FETCH_RESP,
    ST_SEND_TO_CLIENT
} conn_state_t;

typedef struct {
    int fd;
    int upstream_fd;
    conn_state_t state;
    buffer buf;
    int cache_idx;
    int sent_to_client;
    char *req_copy;
    int req_len;
    int req_sent;
    int client_keepalive;
} conn_t;

extern conn_t conns[MAX_CONNS];
extern struct pollfd fds[MAX_CONNS];
extern int listen_fd;

void proxy_init(void);
int proxy_accept_client(void);
void proxy_handle_event(int idx);
void conn_close(int idx);