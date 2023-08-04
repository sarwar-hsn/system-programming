#ifndef FILEQUEUE_H
#define FILEQUEUE_H

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "common.h"

typedef struct Task {
    char filename[MAX_PATH_LENGTH];
    int client_sock;
} task_t;

typedef struct FileQueue {
    task_t tasks[MAX_QUEUE_SIZE];
    int front;
    int rear;
} filequeue_t;

void init_filequeue(filequeue_t* filequeue) {
    filequeue->front = -1;
    filequeue->rear = -1;
}

bool is_filequeue_empty(filequeue_t* filequeue) {
    return filequeue->front == -1 && filequeue->rear == -1;
}

bool is_filequeue_full(filequeue_t* filequeue) {
    return ((filequeue->rear + 1) % MAX_QUEUE_SIZE) == filequeue->front;
}

// Returns -1 on failure, otherwise a positive value
int enqueue_task(filequeue_t* filequeue, task_t task) {
    if (is_filequeue_full(filequeue)) {
        return -1;
    }

    if (is_filequeue_empty(filequeue)) {
        filequeue->front = 0;
        filequeue->rear = 0;
    } else {
        filequeue->rear = (filequeue->rear + 1) % MAX_QUEUE_SIZE;
    }

    filequeue->tasks[filequeue->rear] = task;
    return 1;
}

// Returns task with -1 on failure, otherwise the task
task_t dequeue_task(filequeue_t* filequeue) {
    if (is_filequeue_empty(filequeue)) {
        return (task_t){.client_sock = -1};
    }

    task_t task = filequeue->tasks[filequeue->front];

    if (filequeue->front == filequeue->rear) {
        filequeue->front = -1;
        filequeue->rear = -1;
    } else {
        filequeue->front = (filequeue->front + 1) % MAX_QUEUE_SIZE;
    }

    return task;
}

int get_filequeue_size(filequeue_t* filequeue) {
    if (is_filequeue_empty(filequeue)) {
        return 0;
    } else {
        return (filequeue->rear - filequeue->front + MAX_QUEUE_SIZE) % MAX_QUEUE_SIZE + 1;
    }
}

#endif
