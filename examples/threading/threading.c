#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    struct thread_data* args = (struct thread_data *) thread_param;
    usleep(args->wait_obtain * 1000);
    if (pthread_mutex_lock(args->mutex) != 0) {
        args->thread_complete_success = false;
    } else {
        usleep(args->wait_release * 1000);
        args->thread_complete_success = pthread_mutex_unlock(args->mutex) == 0;
    }
    return thread_param;
}


bool start_thread_obtaining_mutex(thread, mutex, wait_obtain, wait_release)
    pthread_t *thread;
    pthread_mutex_t *mutex;
    int wait_obtain;    /* in ms */
    int wait_release;   /* in ms */
{
    struct thread_data *data = malloc(sizeof (struct thread_data));
    data->wait_obtain = wait_obtain;
    data->wait_release = wait_release;
    data->mutex = mutex;

    return pthread_create(thread, NULL, threadfunc, data) == 0;
}
