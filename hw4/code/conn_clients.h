#ifndef CONNECTED_CLIENTS_H
#define CONNECTED_CLIENTS_H

#include <stdlib.h>

typedef struct {
    int *array;    // Array to store process IDs
    int capacity;  // Maximum capacity of array
    int size;      // Current number of elements in array
} Connclients;

// Initialize the structure
Connclients* init_connclients(int capacity) {
    Connclients *clients = (Connclients*) malloc(sizeof(Connclients));
    clients->capacity = capacity;
    clients->size = 0;
    clients->array = (int*) malloc(sizeof(int) * capacity);
    return clients;
}

// Add a client to the array
int add_connclient(Connclients *clients, int clientPid) {
    if (clients->size >= clients->capacity) {
        return -1;
    }
    clients->array[clients->size++] = clientPid;
    return 0;
}

// -1 in fail else index
int find_connclient(Connclients *clients, int clientPid) {
    for (int i = 0; i < clients->size; i++) {
        if (clients->array[i] == clientPid) {
            return i;
        }
    }
    return -1; 
}

//-1 in fail
int remove_connclient(Connclients *clients, int clientPid) {
    int index = find_connclient(clients, clientPid);
    if (index == -1) {
        return -1;  // Client not found
    }
    // Shift all elements to the left to fill the gap
    for (int i = index; i < clients->size - 1; i++) {
        clients->array[i] = clients->array[i + 1];
    }
    clients->size--; 

    return 0;  
}

// Clean up the structure
void free_connclients(Connclients *clients) {
    free(clients->array);
    free(clients);
}


#endif