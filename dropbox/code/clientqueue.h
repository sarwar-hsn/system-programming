#ifndef CLIENTQUEUE_H 
#define CLIENTQUEUE_H

#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "common.h"




typedef struct ClientQueue {
    int clientSockets[MAX_QUEUE_SIZE];
    int front;
    int rear;
} clientqueue_t;

void init_clientqueue(clientqueue_t* clientqueue) {
    clientqueue->front = -1;
    clientqueue->rear = -1;
}

bool is_clientqueue_empty(clientqueue_t* clientqueue) {
    return clientqueue->front == -1 && clientqueue->rear == -1;
}

bool is_clientqueue_full(clientqueue_t* clientqueue) {
    return ((clientqueue->rear + 1) % MAX_QUEUE_SIZE) == clientqueue->front;
}

// Returns -1 on failure, otherwise a positive value
int enqueue_client(clientqueue_t* clientqueue, int clientSocket) {
    if (is_clientqueue_full(clientqueue)) {
        return -1;
    }

    if (is_clientqueue_empty(clientqueue)) {
        clientqueue->front = 0;
        clientqueue->rear = 0;
    } else {
        clientqueue->rear = (clientqueue->rear + 1) % MAX_QUEUE_SIZE;
    }

    clientqueue->clientSockets[clientqueue->rear] = clientSocket;
    return 1;
}

// Returns -1 on failure, otherwise the clientSocket
int dequeue_client(clientqueue_t* clientqueue) {
    if (is_clientqueue_empty(clientqueue)) {
        return -1;
    }

    int clientSocket = clientqueue->clientSockets[clientqueue->front];

    if (clientqueue->front == clientqueue->rear) {
        clientqueue->front = -1;
        clientqueue->rear = -1;
    } else {
        clientqueue->front = (clientqueue->front + 1) % MAX_QUEUE_SIZE;
    }

    return clientSocket;
}

int get_clientqueue_size(clientqueue_t* clientqueue) {
    if (is_clientqueue_empty(clientqueue)) {
        return 0;
    } else {
        return (clientqueue->rear - clientqueue->front + MAX_QUEUE_SIZE) % MAX_QUEUE_SIZE + 1;
    }
}

#endif
