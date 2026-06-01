#include "http.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>

static char *find_header_ci(const char *headers, size_t hlen, const char *name, size_t *val_len) {
    const char *end = headers + hlen;
    const char *p = headers;
    size_t nlen = strlen(name);
    
    while ((p = memchr(p, '\n', end - p)) != NULL) {
        p++;
        
        if (p < end && strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            p += nlen + 1;
            
            while (p < end && (*p == ' ' || *p == '\t')) {
                p++;
            }
            
            const char *val_end = memchr(p, '\r', end - p);
            if (!val_end) {
                val_end = memchr(p, '\n', end - p);
            }
            if (!val_end) {
                val_end = end;
            }
            
            *val_len = val_end - p;
            return (char *)p;
        }
        
        if (p >= end) {
            break;
        }
    }
    
    return NULL;
}

int http_parse_request(const char *buf, size_t len, char *host, int *port, char *path) {
    (void)len;
    
    char method[16];
    char url[512];
    
    if (sscanf(buf, "%15s %511s", method, url) != 2) {
        return -1;
    }
    
    if (strcmp(method, "GET") != 0) {
        return -2;
    }
    
    if (strncmp(url, "http://", 7) != 0) {
        return -3;
    }
    
    const char *hstart = url + 7;
    const char *pend = strchr(hstart, '/');
    const char *pport = strchr(hstart, ':');
    
    const char *hend;
    if (pend && (!pport || pport < pend)) {
        hend = pport ? pport : pend;
    } else if (pport) {
        hend = pport;
    } else {
        hend = hstart + strlen(hstart);
    }
    
    size_t hlen = hend - hstart;
    if (hlen >= 256) {
        return -4;
    }
    
    strncpy(host, hstart, hlen);
    host[hlen] = '\0';
    
    if (pport && (!pend || pport < pend)) {
        *port = atoi(pport + 1);
        if (*port <= 0 || *port > 65535) {
            *port = 80;
        }
    } else {
        *port = 80;
    }
    
    if (pend) {
        strncpy(path, pend, 511);
    } else {
        strcpy(path, "/");
    }
    path[511] = '\0';
    
    return 0;
}

int http_parse_content_length(const char *headers, size_t hlen) {
    size_t val_len;
    char *val = find_header_ci(headers, hlen, "Content-Length", &val_len);
    
    if (!val) {
        return -1;
    }
    
    return (int)strtol(val, NULL, 10);
}

int http_parse_connection_close(const char *headers, size_t hlen) {
    size_t val_len;
    char *val = find_header_ci(headers, hlen, "Connection", &val_len);
    
    if (!val) {
        return 0;
    }
    
    return strncasecmp(val, "close", val_len) == 0;
}

int http_request_wants_keepalive(const char *req, size_t rlen) {
    size_t val_len;
    
    char *val = find_header_ci(req, rlen, "Proxy-Connection", &val_len);
    if (val) {
        return strncasecmp(val, "keep-alive", val_len) == 0;
    }
    
    val = find_header_ci(req, rlen, "Connection", &val_len);
    if (val) {
        return strncasecmp(val, "keep-alive", val_len) == 0;
    }
    
    return memmem(req, rlen, "HTTP/1.1", 8) != NULL;
}

void http_send_error(int fd, int code, const char *msg) {
    char resp[256];
    
    int n = snprintf(resp, sizeof(resp),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "Content-Length: %lu\r\n"
        "\r\n"
        "%s",
        code, msg, strlen(msg), msg);
    
    send(fd, resp, n, MSG_NOSIGNAL);
}
