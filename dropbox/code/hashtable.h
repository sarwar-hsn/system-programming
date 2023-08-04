#ifndef HASHTABLE_H 
#define HASHTABLE_H


#include <time.h>
#include "common.h"

#define MAX_TABLE_SIZE 1024


typedef struct Fileinfo {
    char filename[MAX_PATH_LENGTH];
    int is_dir;
    int is_updated;
    time_t last_mod_time;  // timestamp of the last modification
} fileinfo_t;


typedef struct hashtable_item {
    char* key;
    fileinfo_t value;
    struct hashtable_item* next;
} hashtable_item_t;

typedef struct {
    hashtable_item_t** table;  
    int size;
} hashtable_t;


unsigned int hash(char* key, int size) {
    unsigned int hashval = 0;
    for (int i = 0; *key != '\0'; key++) {
        hashval = *key + (hashval << 5) - hashval;
    }
    return hashval % size;
}

hashtable_t* hashtable_create(int size) {
    hashtable_t* hashtable = malloc(sizeof(hashtable_t));
    hashtable->table = malloc(sizeof(hashtable_item_t*) * size);
    for(int i=0; i<size; i++){
        hashtable->table[i] = NULL;
    }
    hashtable->size = size;
    return hashtable;
}

void hashtable_insert(hashtable_t* hashtable, char* key, fileinfo_t value) {
    unsigned int slot = hash(key, hashtable->size);
    hashtable_item_t* item = malloc(sizeof(hashtable_item_t));
    item->key = strdup(key);
    item->value = value;
    item->next = hashtable->table[slot];
    hashtable->table[slot] = item;
}

fileinfo_t* hashtable_get(hashtable_t* hashtable, char* key) {
    unsigned int slot = hash(key, hashtable->size);
    hashtable_item_t* item;
    for (item = hashtable->table[slot]; item != NULL; item = item->next) {
        if (strcmp(key, item->key) == 0) {
            return &(item->value);
        }
    }
    return NULL;
}

void hashtable_delete(hashtable_t* hashtable, char* key) {
    unsigned int slot = hash(key, hashtable->size);
    hashtable_item_t* item;
    hashtable_item_t* prev = NULL;
    for (item = hashtable->table[slot]; item != NULL; item = item->next) {
        if (strcmp(key, item->key) == 0) {
            if (prev == NULL) {
                hashtable->table[slot] = item->next;
            } else {
                prev->next = item->next;
            }
            free(item->key);
            free(item);
            return;
        }
        prev = item;
    }
}


void hashtable_free(hashtable_t* hashtable) {
    for (int i = 0; i < hashtable->size; i++) {
        hashtable_item_t* item = hashtable->table[i];
        while (item != NULL) {
            hashtable_item_t* temp = item;
            item = item->next;
            free(temp->key);
            free(temp);
        }
    }
    free(hashtable->table);
    free(hashtable);
}


void hashtable_print_updated(hashtable_t* hashtable) {
    for (int i = 0; i < hashtable->size; i++) {
        hashtable_item_t* item = hashtable->table[i];
        while (item != NULL) {
            printf(" is_updated: %d- %s,\n", item->value.is_updated, item->key);
            item = item->next;
        }
    }
}

#endif