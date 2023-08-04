#ifndef JOBQUEUE_H
#define JOBQUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include "app.h"


typedef struct {
    request_t req;
} Job;

typedef struct JobNode {
    Job job;
    struct JobNode* next;
} JobNode;

typedef struct {
    int size;
    JobNode* head;
    JobNode* tail;
} JobQueue;

// Initialize the job queue.
void init_jqueue(JobQueue* queue) {
    queue->size =0;
    queue->head = NULL;
    queue->tail = NULL;
}

// Add a job to the job queue.
void enqueue_jobs(JobQueue* queue, Job job) {
    JobNode* newNode =(JobNode*) malloc(sizeof(JobNode));
    if (newNode == NULL) {
        printf("Memory allocation failed\n");
        return;
    }
    newNode->job = job;
    newNode->next = NULL;

    if (queue->head == NULL) {  // If the queue is empty
        queue->head = newNode;
        queue->tail = newNode;
    } else {  // If the queue is not empty
        queue->tail->next = newNode;
        queue->tail = newNode;
    }
    queue->size+=1;
}

// Remove a job from the job queue.
Job dequeue_jobs(JobQueue* queue) {
    if (queue->head == NULL) {
        return (Job){0};  // Return a default job
    }

    JobNode* temp = queue->head;
    Job job = temp->job;

    queue->head = queue->head->next;
    if (queue->head == NULL) {  // If the queue is now empty
        queue->tail = NULL;
    }
    queue->size-=1;
    free(temp);
    return job;
}

// Deallocate the entire job queue.
void destroy_jqueue(JobQueue* queue) {
    JobNode* current = queue->head;
    while (current != NULL) {
        JobNode* nextNode = current->next;
        free(current);
        current = nextNode;
    }
    queue->head = NULL;
    queue->tail = NULL;
}


#endif