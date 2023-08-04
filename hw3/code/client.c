// client.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include "app.h"
#include "protocols.h"


//global variables
int client_pid;
int server_pid;
char server_fifo[SERVER_FIFO_NAME_LEN];
char client_fifo[CLIENT_FIFO_NAME_LEN];

volatile sig_atomic_t sigFlag = 0; 

void validate_arguments(int* argc,char* argv[]){
    if(*argc < 3){
        fprintf(stderr,"Usage: ./client <Connect/tryConnect> ServerPID\n");
        exit(EXIT_FAILURE);
    }
    //priting argv
    if(!(strcmp(argv[1],"Connect")==0 || strcmp(argv[1],"tryConnect") == 0)){
        fprintf(stderr,"Invalid Argument\n");
        fprintf(stderr,"Usage: ./client <Connect/tryConnect> ServerPID\n");
        exit(EXIT_FAILURE);
    }
    if(((server_pid = atoi(argv[2])) == 0) && (strcmp(argv[2],"0") ==0)){
        fprintf(stderr,"Invalid Server Name\n");
        fprintf(stderr,"Usage: ./client <Connect/tryConnect> ServerPID\n");
        exit(EXIT_FAILURE);
    }
    printf("server id : %d\n",server_pid);
}

void client_input(char command_text[MAX_ARG_LEN]){
    printf("Enter Command: ");
    fgets(command_text, MAX_ARG_LEN, stdin);
    command_text[strcspn(command_text, "\n")] = '\0'; 
}
void send_request(int serverfd,char * commandText){
    request_t request;
    char temp[100];
    int i = 0; 
    for(i = 0; commandText[i]!='\0'; i++){
        if(commandText[i] == ' ' || commandText[i]=='\n'){
            break;
        }
        temp[i]=commandText[i];
    }
    temp[i]='\0';
    // Command cmd = parse_command(commandText);
    request.client_pid= getpid();
    request.action_code = get_protocol(temp);

    strncpy(request.cmd,commandText,strlen(commandText));
    request.cmd[strlen(commandText)]='\0';

    if (write(serverfd, &request, sizeof(request_t)) !=sizeof(request_t)){
        perror("Couldn't write server request");
        exit(EXIT_FAILURE);
    }
}

response_t parse_response(int clientfd){
    response_t response;
    ssize_t resread;
    while(((resread = read(clientfd, &response, sizeof(response_t))) == -1) && (errno == EINTR));
    
    if(resread == 0){
        response.rescode = resComplete;
        strncpy(response.body, "EOF", sizeof(response.body));
    } else if(resread != sizeof(response_t)){
        perror("Couldn't read server response");
        exit(EXIT_FAILURE);
    }
    
    return response;
}



//Opening clientfd. Returns clientfd after opening
int init_clientfd(){
    //Opening clientfd using client template
    snprintf(client_fifo,CLIENT_FIFO_NAME_LEN,CLIENT_FIFO_TEMPLATE,getpid());
    int fd = open(client_fifo,O_RDONLY); //opeing in write only mode
    if(fd == -1){
        perror("Couldn't open clientfd");
        exit(EXIT_FAILURE);
    }
    return fd;
}

void signal_handler(int sig){
    if(sig == SIGINT || sig == SIGTERM){
        sigFlag = 1;
    }
}
void init_signals(){
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = signal_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT,&sa,NULL);
    sigaction(SIGTERM,&sa,NULL);
}
//removed client fifo after using it
void removeFifo(void){
    if (unlink(client_fifo) == -1) {
        perror("Couldn't remove client FIFO");
        exit(EXIT_FAILURE);
    }
}


int main(int argc, char* argv[]) {
    validate_arguments(&argc,argv);
    int serverfd, clientfd;
    int isWaiting; 
    client_pid = getpid(); //server id
    printf("Client PID: %d\n\n",client_pid);
    
    //initializing signals
    init_signals();
    //Creating client_fifo
    umask(0);    
    snprintf(client_fifo,CLIENT_FIFO_NAME_LEN,CLIENT_FIFO_TEMPLATE,client_pid);
    if ((mkfifo(client_fifo, 0644) == -1) && (errno!=EEXIST)) { //creating fifo
        perror("Error creating client FIFO");
        exit(EXIT_FAILURE);
    }

    // Setting up atexit() immediately after client_fifo is ready
    if (atexit(removeFifo) != 0){
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    printf("verifying server and trying to connect...\n");
    //Opening serverfd from servertemplate
    snprintf(server_fifo,SERVER_FIFO_NAME_LEN,SERVER_FIFO_TEMPLATE,server_pid);
    serverfd = open(server_fifo, O_WRONLY);
    if (serverfd == -1) {
        perror("Couldn't find server. Check server id agian");
        exit(EXIT_FAILURE);
    }else{
        printf("Server Found! Asking Server to Establish Connection\n");
    }
    //Preparing request to establish connection
    send_request(serverfd,argv[1]);
    clientfd = init_clientfd();
    response_t server_res = parse_response(clientfd);  
    if(server_res.rescode == connDeclined){
        printf("server says: %s\n",server_res.body);
    }
    if(server_res.rescode == connWaiting){
        printf("server says: %s\n",server_res.body);
        clientfd = init_clientfd();
        printf("Server should let you in when process slot is empty. However not implemented\n");
        server_res = parse_response(clientfd);
    }
    if(server_res.rescode == connEstablished){
        printf("Connection to server %d established...\n\n",server_pid);
        //Main loop for client
        while(!sigFlag){
            //Client Input
            char command_text[MAX_ARG_LEN]; 
            client_input(command_text);
            //Change - call quit here
            if (sigFlag) {
                printf("Quiting program because of signal\n");
                send_request(serverfd,"quit"); //server will remove from process pool
                break;
            }
            //Sending request to server
            send_request(serverfd,command_text);
            //if client want to quit we close after sending quit request to server
            if(strcmp(command_text,"quit")==0){
                break;
            }
            clientfd = init_clientfd();
            //reading what server sent
            printf("server: ");
            while(1){
                response_t server_res = parse_response(clientfd);   
                //priting the response in terminal
                printf("%s\n",server_res.body);
                if(server_res.rescode==resComplete){
                    break;
                }
            }
        }
    }
    close(clientfd);
    exit(EXIT_SUCCESS);
}
