#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ctype.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>

#define SOCKET_PATH "/tmp/somesocket"
#define BUFFER_SIZE 1024
#define CONNECT_REQ_LIMIT 10
#define CLIENT_COUNT_MAX 2

int main() {
    int server_fd;
    if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    unlink(SOCKET_PATH);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(1);
    }

    if (listen(server_fd, CONNECT_REQ_LIMIT) == -1) {
        perror("listen");
        exit(1);
    }

    struct pollfd fds[CLIENT_COUNT_MAX];
    int nfds = 1;
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    while (1) {
        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            perror("poll");
        }
        
        if (fds[0].revents & POLLIN && nfds != CLIENT_COUNT_MAX) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd == -1) {
                perror("accept");
                exit(1);
            }
            fds[nfds].fd = client_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                unsigned char buff[BUFFER_SIZE];
                int n = recv(fds[i].fd, buff, sizeof(buff) - 1, MSG_DONTWAIT);
                if (n > 0) {
                    buff[n] = '\0';
                    for (int j = 0; buff[j] != '\0'; j++) {
                        buff[j] = toupper(buff[j]);
                    }
                    printf("%s", buff);
                    fflush(stdout);
                } else if (n == 0) {
                    close(fds[i].fd);
                    for (int j = i; j < nfds - 1; j++) {
                        fds[j] = fds[j+1];
                    }
                    nfds--;
                    i--;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    perror("recv");
                    exit(1);
                }
            }
        }
    }

    return 0;
}
