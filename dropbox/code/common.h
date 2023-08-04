#ifndef COMMON_H 
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/time.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>


#define MSGLEN 1024
#define MAX_QUEUE_SIZE 1000
#define BUFFER_SIZE 1024

#define SERVER_TEMPLATE "%s"
#define CLIENT_DIR_TEMPLATE "%s"
#define LOG_DIR "LOGS"
#define CLIENT_LOG_DIR_TEMPLATE "LOGS/%s/"

#define LOG_FILE_TEMPLATE "%s/client.log"
#define LOG_DIR_TEMPLATE_LEN (sizeof(CLIENT_LOG_DIR_TEMPLATE)+20)
#define SERVER_TEMPLATE_LEN (sizeof(SERVER_TEMPLATE)+20)
#define CLIENT_TEMPLATE_LEN (sizeof(CLIENT_DIR_TEMPLATE)+20)


#define MAX_PATH_LENGTH 350


typedef enum Protocol{
    normal,dir_type,file_type,fsend_done,succ,fail,connected,terminate,
    sync_done,sync_start,buff_start,buff_data,buff_end,
    dir_created,file_created,dir_deleted,file_deleted,
    sync_wait,
}protocol_t;

typedef struct Response{
    protocol_t code;
    char data[MSGLEN];
}response_t;

typedef struct Request{
    protocol_t code;
    char data[MSGLEN]; 
}request_t;


int isFileModified(time_t previous_mod_time, time_t current_mod_time) {
    if (current_mod_time > previous_mod_time) {
        return 1; // File has been modified
    } else {
        return 0; // File has not been modified
    }
}

void remove_directory_recursively(const char* dir_path) {
    DIR* d = opendir(dir_path);

    if (d == NULL) {
        printf("Cannot open directory '%s': No such file or directory\n", dir_path);
        return;
    }

    struct dirent* dir;
    char path[1024];
    
    while ((dir = readdir(d)) != NULL) {
        struct stat statbuf;
        
        if(strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
            continue;

        snprintf(path, sizeof(path), "%s/%s", dir_path, dir->d_name);

        if(stat(path, &statbuf) == -1) {
            perror("Failed to get file status");
            continue;
        }

        if(S_ISDIR(statbuf.st_mode))
            remove_directory_recursively(path);
        else
            remove(path);
    }

    closedir(d);

    remove(dir_path);
}

#endif 