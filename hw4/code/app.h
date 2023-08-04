// app.h
#ifndef APP_H
#define APP_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "protocols.h"


#define SERVER_FIFO_TEMPLATE "server_%d"
#define SERVER_FIFO_NAME_LEN (sizeof(SERVER_FIFO_TEMPLATE)+120)
#define SERVER_FIFO_TEMPLATE_FOR_CLIENT "%s/server_%d"
#define CLIENT_FIFO_TEMPLATE_FOR_SERVER "../Client/client_fifo_%d"
#define CLIENT_FIFO_TEMPLATE "Client/client_fifo_%d"
#define CLIENT_FIFO_NAME_LEN (sizeof(CLIENT_FIFO_TEMPLATE)+20)
#define CLIENT_LOG_FILE_FORMAT "../Client/%d.log"
#define CLIENT_DIR_FILE_FORMAT "../Client/%s"
#define CMD_LEN 100
#define RESPONSE_LEN 1024
#define MAX_ARGS 10
#define BUFSIZE 1024

typedef struct request{
    pid_t client_pid;
    reqprotocol_t action_code;
    char cmd[CMD_LEN];
} request_t;

typedef struct response{
    resprotocol_t rescode;
    char body[RESPONSE_LEN];
}response_t;

struct server_config{
    int serverpid;
    char serverdir[100];
};

typedef struct Log{
    int clientpid;
    char cmd[CMD_LEN];
    char created_at[150];
}log_t;


typedef struct {
    char command[CMD_LEN];
    int argc;
    char* argv[MAX_ARGS];
} Command;

Command parse_command(char* input) {
    Command cmd = { .argc = 0 };
    char* token = strtok(input, " ");
    
    // Get the command
    if (token != NULL) {
        strncpy(cmd.command, token, CMD_LEN - 1);
        cmd.command[CMD_LEN - 1] = '\0'; // Null-termination
        token = strtok(NULL, " ");
    }

    // Get the arguments
    while (token != NULL && cmd.argc < MAX_ARGS) {
        cmd.argv[cmd.argc] = (char*)malloc(strlen(token) + 1);
        if (cmd.argv[cmd.argc]) {
            strcpy(cmd.argv[cmd.argc], token);
            cmd.argv[cmd.argc][strlen(token)] = '\0'; // Null-termination
            cmd.argc++;
            token = strtok(NULL, " ");
        }
    }
    if (cmd.argc < MAX_ARGS) {
        cmd.argv[cmd.argc] = NULL; // Null-terminate the argv list
    }
    return cmd;
}



void write_server_log(request_t req){
    //checking if there is log file ornot
    int fd;
    fd = open("server.log", O_WRONLY | O_APPEND | O_CREAT | O_EXCL, 0666);
    if (errno == EEXIST) {
        fd = open("server.log", O_WRONLY | O_APPEND, 0666);
    }else{
        return;
    }
    time_t current_time = time(NULL);
    char* c_time_string = ctime(&current_time);
    c_time_string[strcspn(c_time_string, "\n")] = 0;
    log_t logfile;
    logfile.clientpid = req.client_pid;
    strcpy(logfile.cmd,req.cmd);
    strcpy(logfile.created_at,c_time_string);
    int bytes_written;
    do {
        bytes_written = write(fd, &logfile, sizeof(log_t));
    } while (bytes_written == -1 && errno == EINTR);

    if (bytes_written == -1) {
        perror("Error writing to file");
        close(fd);
        return;
    }
    close(fd); 
}



void prepare_client_logfile(int clientpid){
    int fd;
    fd = open("server.log",O_RDONLY);
    if(fd == -1){
        perror("opening serverlog file");
        return;
    }
    log_t logs[1000];
    int logread;
    log_t temp;
    int count = 0; 
    for(;;){
        while(((logread = read(fd,&temp,sizeof(log_t))) ==-1) && (errno == EINTR));
        if(logread <= 0){
            break;
        }else{
            if(temp.clientpid == clientpid){
                logs[count].clientpid = temp.clientpid;
                strcpy(logs[count].cmd,temp.cmd);
                strcpy(logs[count].created_at,temp.created_at);
                count++;
            }
        }
    }
    close(fd);
    
    //writing to client folder
    char clientfilename[150];
    sprintf(clientfilename,CLIENT_LOG_FILE_FORMAT,clientpid);
    int tofd = open(clientfilename,O_WRONLY | O_CREAT, 0666);
    if(tofd == -1){
        perror("opening client log file");
        return;
    }
    for(int i = 0; i < count; i++){
        char string[200];
        strcpy(string,logs[i].created_at);
        strcat(string," ");
        strcat(string,logs[i].cmd);
        strcat(string,"\n");
        int byteswritten;
        while(((byteswritten = write(tofd,string,strlen(string)))==-1) && (errno == EINTR));
        if(byteswritten <= 0){
            break;
        }
    }
    close(tofd);
}

void write_server_config(struct server_config* config){
    int fd = open("config", O_WRONLY | O_CREAT | O_EXCL, 0755);
    if(fd == -1 && errno == EEXIST){
        fd = open("config", O_WRONLY, 0755);
    }
    if(fd==-1){
        perror("server config write failed(1)");
        return;
    }
    if(write(fd, config, sizeof(struct server_config))==-1){
        perror("server config write failed(2)");
        return;
    }
    close(fd);
}

void read_server_config(struct server_config* config){
    int fd = open("config",O_RDONLY);
    if(fd == -1){
        perror("server config read failed(1)");
        exit(EXIT_FAILURE);
    }
    int readbyte = read(fd, config, sizeof(struct server_config));
    if(readbyte == -1){
        perror("server config read failed(2)");
        exit(EXIT_FAILURE);
    }
    close(fd);
}

#endif
