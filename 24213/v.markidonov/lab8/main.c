#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    int fd = open(filename, O_RDWR);
    if (fd == -1) {
        perror("open");
        return 1;
    }

    if (lockf(fd, F_TLOCK, 0) == -1) {
        perror("flock");
        close(fd);
        return 1;
    }

    char command[300];
    sprintf(command, "vim %s", filename);
    if (system(command) == -1) {
        perror("Can't open vim");
    }

    close(fd);
    return 0;
}
