#include "proxy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    
    int listen_port = atoi(argv[1]);
    
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    
    if (listen(listen_fd, 64) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }
    
    int flags = fcntl(listen_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
    }
    
    proxy_init();
    cache_init();
    
    fds[0].fd = listen_fd;
    fds[0].events = POLLIN;
    
    printf("✓ Proxy ready on port %d\n", listen_port);
    
    while (1) {
        int ret = poll(fds, MAX_CONNS, -1);
        
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }
        
        if (fds[0].revents & POLLIN) {
            proxy_accept_client();
        }
        
        for (int i = 1; i < MAX_CONNS; i++) {
            if (conns[i].state != ST_FREE) {
                proxy_handle_event(i);
            }
        }
    }
    
    if (listen_fd >= 0) {
        close(listen_fd);
    }
    
    return 0;
}
