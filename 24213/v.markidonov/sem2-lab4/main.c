#include <pthread.h>
#include <unistd.h>
#include <stdio.h>

void* worker(void* arg) {
    while (1) {
        printf("Child thread is working...\n");
        sleep(1);
    }
}

int main() {
    pthread_t th;
    pthread_create(&th, NULL, worker, NULL);

    sleep(2);
    pthread_cancel(th);
    pthread_join(th, NULL);

    printf("Thread canceled.\n");
    return 0;
}
