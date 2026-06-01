#include "proxy.h"
#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

conn_t conns[MAX_CONNS];
struct pollfd fds[MAX_CONNS];
int listen_fd = -1;

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl F_GETFL");
        return;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl F_SETFL");
    }
}

void proxy_init(void) {
    for (int i = 0; i < MAX_CONNS; i++) {
        conns[i].fd = -1;
        conns[i].upstream_fd = -1;
        conns[i].state = ST_FREE;
        conns[i].cache_idx = -1;
        conns[i].req_copy = NULL;
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }
}

void conn_close(int idx) {
    conn_t *c = &conns[idx];
    
    if (c->fd >= 0) {
        if (c->cache_idx >= 0) {
            if (cache[c->cache_idx].ref_count > 0) {
                cache[c->cache_idx].ref_count--;
            }
        }
        close(c->fd);
    }
    
    if (c->upstream_fd >= 0) {
        close(c->upstream_fd);
    }
    
    buffer_free(&c->buf);
    free(c->req_copy);
    
    c->fd = -1;
    c->upstream_fd = -1;
    c->state = ST_FREE;
    c->cache_idx = -1;
    c->req_copy = NULL;
    fds[idx].fd = -1;
    fds[idx].events = 0;
    fds[idx].revents = 0;
}

int proxy_accept_client(void) {
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        return -1;
    }
    
    set_nonblocking(client_fd);
    
    for (int i = 1; i < MAX_CONNS; i++) {
        if (conns[i].state == ST_FREE) {
            conns[i].fd = client_fd;
            conns[i].state = ST_READ_REQ;
            buffer_init(&conns[i].buf, BUF_CHUNK);
            conns[i].client_keepalive = 0;
            conns[i].cache_idx = -1;
            conns[i].req_copy = NULL;
            fds[i].fd = client_fd;
            fds[i].events = POLLIN;
            fds[i].revents = 0;
            return i;
        }
    }
    
    close(client_fd);
    return -1;
}

static void handle_read_req(int idx) {
    conn_t *c = &conns[idx];
    
    int n = buffer_recv_nb(c->fd, &c->buf, BUF_CHUNK);
    if (n < 0) {
        conn_close(idx);
        return;
    }
    
    char *headers_end = memmem(
        c->buf.buf + c->buf.offset,
        c->buf.size,
        "\r\n\r\n",
        4
    );
    
    if (!headers_end) {
        return;
    }
    
    char host[256] = {0};
    char path[512] = {0};
    int upstream_port = 80;
    
    int parse_result = http_parse_request(
        c->buf.buf + c->buf.offset,
        c->buf.size,
        host,
        &upstream_port,
        path
    );
    
    if (parse_result != 0) {
        http_send_error(c->fd, 400, "Bad Request");
        conn_close(idx);
        return;
    }
    
    char url[1024];
    snprintf(url, sizeof(url), "http://%s:%d%s", host, upstream_port, path);
    
    c->client_keepalive = http_request_wants_keepalive(
        c->buf.buf + c->buf.offset,
        c->buf.size
    );
    
    int cache_idx = cache_find(url);
    if (cache_idx < 0) {
        cache_idx = cache_alloc(url);
    }
    
    if (cache_idx < 0) {
        http_send_error(c->fd, 503, "Cache Full");
        conn_close(idx);
        return;
    }
    
    c->cache_idx = cache_idx;
    cache[cache_idx].ref_count++;
    
    if (cache[cache_idx].is_complete || cache[cache_idx].size > 0) {
        buffer_free(&c->buf);
        c->state = ST_SEND_TO_CLIENT;
        c->sent_to_client = 0;
        fds[idx].events = POLLOUT;
        return;
    }
    
    struct hostent *host_entry = gethostbyname(host);
    if (!host_entry) {
        http_send_error(c->fd, 502, "Bad Gateway (DNS)");
        conn_close(idx);
        return;
    }
    
    int upstream_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (upstream_fd < 0) {
        http_send_error(c->fd, 503, "Socket error");
        conn_close(idx);
        return;
    }
    
    set_nonblocking(upstream_fd);
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(upstream_port);
    memcpy(&server_addr.sin_addr, host_entry->h_addr, host_entry->h_length);
    
    int upstream_slot = -1;
    for (int i = 1; i < MAX_CONNS; i++) {
        if (conns[i].state == ST_FREE) {
            upstream_slot = i;
            break;
        }
    }
    
    if (upstream_slot < 0) {
        close(upstream_fd);
        http_send_error(c->fd, 503, "No free slots");
        conn_close(idx);
        return;
    }
    
    connect(upstream_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    
    conns[upstream_slot].fd = upstream_fd;
    conns[upstream_slot].upstream_fd = upstream_fd;
    conns[upstream_slot].state = ST_CONNECTING;
    conns[upstream_slot].cache_idx = cache_idx;
    
    cache[cache_idx].ref_count++;
    
    conns[upstream_slot].req_copy = strndup(
        c->buf.buf + c->buf.offset,
        c->buf.size
    );
    conns[upstream_slot].req_len = c->buf.size - c->buf.offset;
    conns[upstream_slot].req_sent = 0;
    
    fds[upstream_slot].fd = upstream_fd;
    fds[upstream_slot].events = POLLOUT;
    fds[upstream_slot].revents = 0;
    
    buffer_free(&c->buf);
    c->state = ST_SEND_TO_CLIENT;
    c->sent_to_client = 0;
    fds[idx].events = 0;
}

static void handle_send_req(int idx) {
    conn_t *c = &conns[idx];
    
    int sent = send(
        c->fd,
        c->req_copy + c->req_sent,
        c->req_len - c->req_sent,
        MSG_NOSIGNAL
    );
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        conn_close(idx);
        return;
    }
    
    c->req_sent += sent;
    
    if (c->req_sent >= c->req_len) {
        free(c->req_copy);
        c->req_copy = NULL;
        c->state = ST_FETCH_RESP;
        fds[idx].events = POLLIN;
    }
}

static void handle_connecting(int idx) {
    conn_t *c = &conns[idx];
    
    int error = 0;
    socklen_t error_len = sizeof(error);
    
    if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &error, &error_len) < 0) {
        conn_close(idx);
        return;
    }
    
    if (error != 0) {
        int cache_idx = c->cache_idx;
        for (int i = 1; i < MAX_CONNS; i++) {
            if (conns[i].state == ST_SEND_TO_CLIENT &&
                conns[i].cache_idx == cache_idx) {
                http_send_error(conns[i].fd, 502, "Connect failed");
                conn_close(i);
            }
        }
        conn_close(idx);
        return;
    }
    
    c->state = ST_SEND_REQ;
    handle_send_req(idx);
}

static void handle_fetch_resp(int idx) {
    conn_t *c = &conns[idx];
    int cache_idx = c->cache_idx;
    
    char chunk[BUF_CHUNK];
    int n = recv(c->fd, chunk, sizeof(chunk), 0);
    
    if (n > 0) {
        if (cache[cache_idx].size + n > cache[cache_idx].capacity) {
            cache[cache_idx].capacity = (cache[cache_idx].size + n) * 2;
            cache[cache_idx].data = realloc(
                cache[cache_idx].data,
                cache[cache_idx].capacity
            );
            if (!cache[cache_idx].data) {
                perror("realloc cache");
                exit(1);
            }
        }
        
        memcpy(
            cache[cache_idx].data + cache[cache_idx].size,
            chunk,
            n
        );
        cache[cache_idx].size += n;
        
        if (cache[cache_idx].headers_end == 0) {
            char *he = memmem(
                cache[cache_idx].data,
                cache[cache_idx].size,
                "\r\n\r\n",
                4
            );
            if (he) {
                cache[cache_idx].headers_end = (he - cache[cache_idx].data) + 4;
                cache[cache_idx].expected_body = http_parse_content_length(
                    cache[cache_idx].data,
                    cache[cache_idx].headers_end
                );
            }
        }
        
        if (cache[cache_idx].headers_end > 0 &&
            cache[cache_idx].expected_body >= 0) {
            
            int body_received = cache[cache_idx].size - cache[cache_idx].headers_end;
            if (body_received >= cache[cache_idx].expected_body) {
                cache[cache_idx].is_complete = 1;
            }
        }
        
        for (int i = 1; i < MAX_CONNS; i++) {
            if (conns[i].state == ST_SEND_TO_CLIENT &&
                conns[i].cache_idx == cache_idx) {
                fds[i].events = POLLOUT;
            }
        }
        
    } else if (n == 0) {
        if (cache[cache_idx].expected_body < 0) {
            cache[cache_idx].is_complete = 1;
        }
        
        for (int i = 1; i < MAX_CONNS; i++) {
            if (conns[i].state == ST_SEND_TO_CLIENT &&
                conns[i].cache_idx == cache_idx) {
                fds[i].events = POLLOUT;
            }
        }
        
        conn_close(idx);
        
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        for (int i = 1; i < MAX_CONNS; i++) {
            if (conns[i].state == ST_SEND_TO_CLIENT &&
                conns[i].cache_idx == cache_idx) {
                http_send_error(conns[i].fd, 502, "Upstream error");
                conn_close(i);
            }
        }
        conn_close(idx);
    }
}

static void handle_send_to_client(int idx) {
    conn_t *c = &conns[idx];
    int cache_idx = c->cache_idx;
    
    int available = cache[cache_idx].size - c->sent_to_client;
    
    if (available > 0) {
        int sent = send(
            c->fd,
            cache[cache_idx].data + c->sent_to_client,
            available,
            MSG_NOSIGNAL
        );
        
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            conn_close(idx);
            return;
        }
        
        c->sent_to_client += sent;
    }
    
    if (c->sent_to_client >= cache[cache_idx].size) {
        
        if (cache[cache_idx].is_complete) {
            
            if (c->client_keepalive &&
                !http_parse_connection_close(
                    cache[cache_idx].data,
                    cache[cache_idx].headers_end
                )) {
                
                if (cache[cache_idx].ref_count > 0) {
                    cache[cache_idx].ref_count--;
                }
                
                c->state = ST_READ_REQ;
                c->sent_to_client = 0;
                c->cache_idx = -1;
                buffer_init(&c->buf, BUF_CHUNK);
                fds[idx].events = POLLIN;
                
            } else {
                conn_close(idx);
            }
            
        } else {
            fds[idx].events = 0;
        }
    }
}

void proxy_handle_event(int idx) {
    conn_t *c = &conns[idx];
    
    if (fds[idx].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        conn_close(idx);
        return;
    }
    
    switch (c->state) {
        case ST_READ_REQ:
            if (fds[idx].revents & POLLIN) {
                handle_read_req(idx);
            }
            break;
            
        case ST_CONNECTING:
            if (fds[idx].revents & POLLOUT) {
                handle_connecting(idx);
            }
            break;
            
        case ST_SEND_REQ:
            if (fds[idx].revents & POLLOUT) {
                handle_send_req(idx);
            }
            break;
            
        case ST_FETCH_RESP:
            if (fds[idx].revents & POLLIN) {
                handle_fetch_resp(idx);
            }
            break;
            
        case ST_SEND_TO_CLIENT:
            if (fds[idx].revents & POLLOUT) {
                handle_send_to_client(idx);
            }
            break;
            
        default:
            break;
    }
}
