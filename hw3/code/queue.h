#ifndef QUEUE_H
#define QUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>


typedef struct Node {
    int data;
    struct Node* next;
} Node;

Node* newNode(int data){
    Node* temp =(Node*)malloc(sizeof(Node));
    temp->data = data;
    temp->next = NULL;
    return temp;
}



typedef struct Queue {
    struct Node* front, *rear;
    int size;
} Queue;

Queue* createQueue() {
    Queue* queue = (Queue*)malloc(sizeof(Queue));
    queue->front = queue->rear = NULL;
    queue->size = 0;
    return queue;
}

void enqueue(Queue* queue, int data) {
    Node* node = newNode(data);

    if (queue->rear == NULL) {
        queue->front = queue->rear = node;
        queue->size++;
        return;
    }

    queue->rear->next = node;
    queue->rear = node;
    queue->size++;
}

int dequeue(Queue* queue) {
    if (queue->front == NULL)
        return INT_MIN;

    Node* temp = queue->front;
    int data = temp->data;

    queue->front = queue->front->next;
    if (queue->front == NULL)
        queue->rear = NULL;

    free(temp);
    queue->size--;
    return data;
}

int peek(Queue* queue) {
    if (queue->front == NULL)
        return INT_MIN;
    return queue->front->data;
}

int isEmpty(Queue* queue) {
    return queue->front == NULL;
}

int size(Queue* queue) {
    return queue->size;
}


struct Node* removeItem(struct Queue* queue, int data) {
    // Check if queue is empty
    if (isEmpty(queue)) {
        return NULL;
    }

    // Check if the item to remove is at the front of the queue
    if (queue->front->data == data) {
        struct Node* temp = queue->front;
        queue->front = queue->front->next;

        if (queue->front == NULL) {
            queue->rear = NULL;
        }
        
        queue->size--;
        return temp;
    }

    // If not at the front, check the rest of the queue
    struct Node* current = queue->front;
    while (current->next != NULL && current->next->data != data) {
        current = current->next;
    }

    // If we found the item to remove
    if (current->next != NULL) {
        struct Node* temp = current->next;
        current->next = current->next->next;

        // If we removed the last item, update the rear pointer
        if (current->next == NULL) {
            queue->rear = current;
        }
        
        queue->size--;
        return temp;
    }

    // If the item was not found, return null
    return NULL;
}

int is_in_queue(Queue* q, int pid) {
    Node* current = q->front;
    while(current != NULL) {
        if(current->data == pid) {
            return 1;  // Return true if the value is found in the queue.
        }
        current = current->next;
    }
    return 0;  // Return false if the value is not found in the queue.
}




#endif