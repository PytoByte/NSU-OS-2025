#include "cache.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

cache_entry_t cache[MAX_CACHE];
int cache_count = 0;

static unsigned cache_seq = 0;

static void normalize_url(const char *src, char *dst, size_t dst_sz) {
    if (strncmp(src, "http://", 7) != 0) {
        strncpy(dst, src, dst_sz - 1);
        dst[dst_sz - 1] = '\0';
        return;
    }
    
    strncpy(dst, "http://", dst_sz);
    
    const char *hstart = src + 7;
    const char *pend = strchr(hstart, '/');
    const char *pport = strchr(hstart, ':');
    
    const char *hend;
    if (pend && (!pport || pport > pend)) {
        hend = pend;
    } else if (pport) {
        hend = pport;
    } else {
        hend = hstart + strlen(hstart);
    }
    
    size_t host_len = hend - hstart;
    
    for (size_t i = 0; i < host_len && i < dst_sz - 8; i++) {
        dst[7 + i] = tolower((unsigned char)hstart[i]);
    }
    dst[7 + host_len] = '\0';
    
    if (pend) {
        strncat(dst, pend, dst_sz - strlen(dst) - 1);
    }
}

void cache_init(void) {
    cache_count = 0;
    cache_seq = 0;
    memset(cache, 0, sizeof(cache));
}

int cache_find(const char *url) {
    char norm[512];
    normalize_url(url, norm, sizeof(norm));
    
    for (int i = 0; i < cache_count; i++) {
        if (strcmp(cache[i].url, norm) == 0) {
            return i;
        }
    }
    
    return -1;
}

int cache_alloc(const char *url) {
    if (cache_count >= MAX_CACHE) {
        int oldest = -1;
        unsigned oldest_seq = UINT_MAX;
        
        for (int i = 0; i < cache_count; i++) {
            if (cache[i].ref_count == 0 && cache[i].seq < oldest_seq) {
                oldest_seq = cache[i].seq;
                oldest = i;
            }
        }
        
        if (oldest < 0) {
            return -1;
        }
        
        free(cache[oldest].data);
        
        if (oldest != cache_count - 1) {
            cache[oldest] = cache[cache_count - 1];
        }
        
        cache_count--;
    }
    
    int idx = cache_count++;
    
    memset(&cache[idx], 0, sizeof(cache_entry_t));
    normalize_url(url, cache[idx].url, sizeof(cache[idx].url));
    cache[idx].seq = cache_seq++;
    cache[idx].expected_body = -1;
    
    return idx;
}
