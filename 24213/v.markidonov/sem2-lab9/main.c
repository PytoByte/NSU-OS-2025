#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>

#define CHECK 1000000

typedef struct {
    long long start;
    double sum;
} calc_request;

int threads_count = 0;

pthread_barrier_t barrier;
atomic_int stop_flag = ATOMIC_VAR_INIT(0);
volatile int agreed_stop = 0;

void sigint_handler(int sig) {
    atomic_store_explicit(&stop_flag, 1, memory_order_relaxed);
}

void* calculate(void* calc_req) {
    calc_request* request = (calc_request*)calc_req;
    
    long long i = request->start;
    long long iteration = 0;
    while (1) {
        request->sum += 1.0 / (i * 4.0 + 1.0);
        request->sum -= 1.0 / (i * 4.0 + 3.0);
        i += threads_count;
        iteration++;
        if (iteration % CHECK == 0) {
            int rc = pthread_barrier_wait(&barrier);
            if (rc == PTHREAD_BARRIER_SERIAL_THREAD) {
                agreed_stop = atomic_load_explicit(&stop_flag, memory_order_relaxed);
            }
            pthread_barrier_wait(&barrier);
            if (agreed_stop) {
                break;
            }
        }

        if (iteration % CHECK == 0) {
            pthread_barrier_wait(&barrier);
            if (atomic_load_explicit(&stop_flag, memory_order_relaxed)) {
                break;
            }
        }
    }

    return calc_req;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <threads>\n", argv[0]);
        return -1;
    }

    threads_count = atoi(argv[1]);
    if (threads_count <= 0) {
        fprintf(stderr, "incorrect number of threads\n");
        return -1;
    }

    pthread_barrier_init(&barrier, NULL, (unsigned)threads_count);

    pthread_t threads[threads_count];
    calc_request calc_requests[threads_count];
    int code = 0;

    for (int i = 0; i < threads_count; i++) {
        calc_requests[i].start = i;
        calc_requests[i].sum = 0.0;
        code = pthread_create(&threads[i], NULL, calculate, (void*)&calc_requests[i]);
        if (code != 0) {
            fprintf(stderr, "creating thread: %s\n", strerror(code));
            pthread_barrier_destroy(&barrier);
            return -1;
        }
    }

    double result = 0.0;
    for (int i = 0; i < threads_count; i++) {
        calc_request *calc_response;
        code = pthread_join(threads[i], (void**)&calc_response);
        if (code != 0) {
            fprintf(stderr, "joining thread: %s\n", strerror(code));
            pthread_barrier_destroy(&barrier);
            return -1;
        }
        result += calc_response->sum;
    }
    result *= 4.0;

    printf("%.15g\n", result);
    return 0;
}
