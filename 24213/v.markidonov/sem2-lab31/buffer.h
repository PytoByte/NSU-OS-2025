#pragma once

typedef struct {
    char *buf;
    int capacity;
    int size;
    int offset;
} buffer;

void buffer_init(buffer *b, int capacity);
void buffer_free(buffer *b);
int buffer_recv_nb(int fd, buffer *b, int chunk);
