#pragma once

#include <stddef.h>

int http_parse_request(const char *buf, size_t len, char *host, int *port, char *path);
int http_parse_content_length(const char *headers, size_t hlen);
int http_parse_connection_close(const char *headers, size_t hlen);
int http_request_wants_keepalive(const char *req, size_t rlen);
void http_send_error(int fd, int code, const char *msg);
