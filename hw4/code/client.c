#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include "app.h"
#include "protocols.h"


//global variables
int client_pid;
int server_pid;
char server_fifo[SERVER_FIFO_NAME_LEN];
char client_fifo[CLIENT_FIFO_NAME_LEN];


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

void client_input(char command_text[CMD_LEN]){
    printf("Enter Command: ");
    fgets(command_text, CMD_LEN, stdin);
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


//removed client fifo after using it
void removeFifo(void){
    unlink(client_fifo);
}

int main(int argc, char* argv[]) {    
    validate_arguments(&argc,argv);
    int serverfd, clientfd; 
    
    //creating client folder if doesn't exists
    if(mkdir("Client",0755)==-1 && errno!=EEXIST){
        perror("client folder");
        exit(EXIT_FAILURE);
    }
    //Creating client_fifo
    umask(0);
    client_pid = getpid(); //server id
    snprintf(client_fifo,CLIENT_FIFO_NAME_LEN,CLIENT_FIFO_TEMPLATE,client_pid);
    if ((mkfifo(client_fifo, 0644) == -1) && (errno!=EEXIST)) { //creating fifo
        perror("Error creating client FIFO");
        exit(EXIT_FAILURE);
    }
    
    //reading serverconfig file
    struct server_config config;
    read_server_config(&config);

    printf("verifying server and trying to connect...\n");
    //Opening serverfd from servertemplate
    snprintf(server_fifo,SERVER_FIFO_NAME_LEN, SERVER_FIFO_TEMPLATE_FOR_CLIENT,config.serverdir,server_pid);
    serverfd = open(server_fifo, O_WRONLY);
    if (serverfd == -1) {
        perror("Couldn't find server. Check server id agian");
        exit(EXIT_FAILURE);
    }else{
        printf("Server Found!\n");
        printf("Asking Server to Establish Connection\n");
    }

    //removing fifo atexit 
    if (atexit(removeFifo) != 0){
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    send_request(serverfd,argv[1]);
    clientfd = init_clientfd();
    response_t server_res = parse_response(clientfd);  
    printf("server: %s\n",server_res.body);
    if(server_res.rescode == connDeclined){
        printf("server says: %s\n",server_res.body);
    }
    if(server_res.rescode == connWaiting){
        printf("server says: %s\n",server_res.body);
        printf("Server should let you in when process slot is empty. However not implemented\n");
        server_res = parse_response(clientfd);
    }

    if(server_res.rescode == connEstablished){
        printf("Connection to server %d established...\n\n",server_pid);
        //Main loop for client
        while(1){
            //Client Input
            char command_text[CMD_LEN]; 
            client_input(command_text);
            
            //Sending request to server
            send_request(serverfd,command_text);
            
            clientfd = init_clientfd();
            //reading what server sent
            printf("server: ");
            int isTerminate= 0;
            while(1){
                response_t server_res = parse_response(clientfd);   
                //priting the response in terminal
                printf("%s\n",server_res.body);
                if(server_res.rescode==resComplete || server_res.rescode == resFail){
                    break;
                }
                if(server_res.rescode == connClosed){
                    isTerminate =1;
                    break;
                }
            }
            //if client want to quit we close after sending quit request to server
            if(strcmp(command_text,"quit")==0 || isTerminate==1){
                break;
            }
        }
    }
    close(clientfd);
    unlink(client_fifo);
    exit(EXIT_SUCCESS);
}
