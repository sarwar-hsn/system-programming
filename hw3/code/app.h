// app.h
#ifndef APP_H
#define APP_H


#include "protocols.h"

#define SERVER_FIFO_TEMPLATE "APP/server_%d"
#define CLIENT_FIFO_TEMPLATE "APP/client_fifo_%d"
#define CLIENT_DIR_NAME_LEN (sizeof(CLIENT_DIR_TEMPLATE)+20)
#define SERVER_FIFO_NAME_LEN (sizeof(SERVER_FIFO_TEMPLATE)+20)
#define CLIENT_FIFO_NAME_LEN (sizeof(CLIENT_FIFO_TEMPLATE)+20)
#define RESPONSE_LEN 1024
#define MAX_ARGS 10
#define MAX_ARG_LEN 256

typedef struct request{
    pid_t client_pid;
    reqprotocol_t action_code;
    char cmd[MAX_ARG_LEN];
} request_t;

typedef struct response{
    resprotocol_t rescode;
    char body[RESPONSE_LEN];
}response_t;


typedef struct {
    char command[MAX_ARG_LEN];
    int argc;
    char* argv[MAX_ARGS];
} Command;

Command parse_command(char* input) {
    Command cmd = { .argc = 0 };
    char* token = strtok(input, " ");
    
    // Get the command
    if (token != NULL) {
        strncpy(cmd.command, token, MAX_ARG_LEN - 1);
        cmd.command[MAX_ARG_LEN - 1] = '\0'; // Null-termination
        token = strtok(NULL, " ");
    }

    // Get the arguments
    while (token != NULL && cmd.argc < MAX_ARGS) {
        cmd.argv[cmd.argc] = malloc(strlen(token) + 1);
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


void copystring(char * source, char * dest)
{
    char * temp1 = source;
    char * temp2 = dest;
    while(*temp1 != '\0')
    {
        *temp2 = *temp1;
        temp1++;
        temp2++;
    }
    *temp2 = '\0';
}


void free_command(Command* cmd) {
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
}





//fail -1, success num
int validate_posnum(char * number){
    int num;
    if(((num =atoi(number)) == 0) && (strcmp(number,"0") ==0)){
        return -1;
    }
    if(num < 0){
        return -1;
    }
    return num;
}

char* usage_fileR(){
    return "readF <file> <line #>";
}

#endif
