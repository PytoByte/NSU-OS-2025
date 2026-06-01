#include "buffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>

void buffer_init(buffer *b, int capacity) {
    b->buf = malloc(capacity);
    if (!b->buf) {
        perror("malloc");
        exit(1);
    }
    b->capacity = capacity;
    b->size = 0;
    b->offset = 0;
    b->buf[0] = '\0';
}

void buffer_free(buffer *b) {
    free(b->buf);
    b->buf = NULL;
    b->capacity = 0;
    b->size = 0;
    b->offset = 0;
}

static void extend(buffer *b, int size) {
    if (b->capacity - b->offset - b->size >= size + 1) {
        return;
    }
    
    int new_cap = (b->capacity < size + 1) ? (size + 1) * 2 : b->capacity * 2;
    char *new_buf = realloc(b->buf, new_cap);
    
    if (!new_buf) {
        perror("realloc");
        exit(1);
    }
    
    b->buf = new_buf;
    b->capacity = new_cap;
}

int buffer_recv_nb(int fd, buffer *b, int chunk) {
    extend(b, chunk);
    
    int n = recv(fd, b->buf + b->offset + b->size, chunk, 0);
    
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    
    if (n == 0) {
        return -2;
    }
    
    b->size += n;
    b->buf[b->offset + b->size] = '\0';
    
    return n;
}
