#pragma once

#include <stddef.h>

#define MAX_CACHE 128

typedef struct {
    char url[512];
    char *data;
    int size;
    int capacity;
    int headers_end;
    int expected_body;
    int is_complete;
    int ref_count;
    unsigned seq;
} cache_entry_t;

extern cache_entry_t cache[MAX_CACHE];
extern int cache_count;

void cache_init(void);
int cache_find(const char *url);
int cache_alloc(const char *url);
