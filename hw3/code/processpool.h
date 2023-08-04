#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <semaphore.h>

#define MAX_CLIENT 10



typedef struct {
    int arr[MAX_CLIENT];  // Array to store process IDs
    int capacity;  // Maximum capacity of array
    int size;      // Current number of elements in array
} ProcessPool;


void initProcessPool(ProcessPool **pool) {
    int fd = shm_open("/myprocesspool", O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(fd, sizeof(ProcessPool)) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }

    *pool = (ProcessPool*) mmap(NULL, sizeof(ProcessPool), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (*pool == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    close(fd);

    (*pool)->capacity = MAX_CLIENT;
    (*pool)->size = 0;
}

//return 1 in succes else 0
int addProcess(ProcessPool *pool, int pid) {
    // Check if pool is full
    if (pool->size == pool->capacity) {
        return 0;
    }
    pool->arr[pool->size++] = pid;
    return 1;
}


int findProcess(ProcessPool *pool, int pid) {
    for (int i = 0; i < pool->size; i++) {
        if (pool->arr[i] == pid) {
            return i;
        }
    }
    return -1;  // Not found
}


//returns 0 on fail else 1
int removeProcess(ProcessPool *pool, int pid) {
    int index = findProcess(pool, pid);
    if (index == -1) {
        return 0;
    }
    // Shift elements to fill the spot of the removed process
    for (int i = index; i < pool->size - 1; i++) {
        pool->arr[i] = pool->arr[i + 1];
    }
    pool->size--;
    return 1;
}

void print_pool(ProcessPool* pool){
    for(int i = 0; i < pool->size; i++){
        printf("processpids: %d,",pool->arr[i]);
    }
    printf("\n");
}



#endif