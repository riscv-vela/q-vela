#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include "rocc_api.h"

#include <sys/syscall.h>
#include <unistd.h>

void *pthread_init_and_dump_trace(void *ptr){
    printf("thread_start called for TID %ld\n", syscall(SYS_gettid));
    gemmini_init_trace();
    gemmini_dump_trace();
    pthread_exit((void*)100);
}

int main(void){
    pthread_t thread1, thread2;
    int  ret, status;
    ret = pthread_create(&thread1, NULL, pthread_init_and_dump_trace, NULL);
    printf("Thread creation returned: %d\n", ret);
    ret = pthread_create(&thread2, NULL, pthread_init_and_dump_trace, NULL);
    printf("Thread creation returned: %d\n", ret);
    pthread_join(thread1, (void**)&status);
    printf("Thread 1 exit status: %d\n", status);
    pthread_join(thread2, (void**)&status);
    printf("Thread 2 exit status: %d\n", status);
    return 0;
}