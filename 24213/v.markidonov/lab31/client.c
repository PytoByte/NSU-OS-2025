#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/somesocket"
#define BUFFER_SIZE 1024

int main() {
    int socket_fd;
    if ((socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCKET_PATH);

    if (connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(socket_fd);
        exit(1);
    }

    char buffer[BUFFER_SIZE + 1];
    while (fgets(buffer, BUFFER_SIZE + 1, stdin)) {
        ssize_t sended = send(socket_fd, buffer, strlen(buffer), 0);
        if (sended < 0) {
            perror("send");
            close(socket_fd);
            return 1;
        }
    }

    close(socket_fd);
    
    return 0;
}