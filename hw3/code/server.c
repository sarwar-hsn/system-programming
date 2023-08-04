#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <wait.h>
#include <sys/mman.h>
#include <dirent.h>
#include <semaphore.h>
#include <time.h>
#include "app.h"
#include "queue.h"
#include "protocols.h"
#include "processpool.h"


// Function prototypes
void sigint_handler(int sig);
void cleanup();
void execute_readF(Command,int);
void execute_writeT(Command,int);
void execute_list(int);
void execute_upload(Command,int);

// Global variables
int server_pid;
char server_fifo[SERVER_FIFO_NAME_LEN];
char client_fifo[CLIENT_FIFO_NAME_LEN];
int current_clients = 0;


//signal flags
volatile sig_atomic_t flag = 0;
// Global semaphores
sem_t *mutex;
sem_t *empty;
sem_t *full;

void init_semaphores(){
    mutex = sem_open("/mutex", O_CREAT, 0644, 1);
    empty = sem_open("/empty", O_CREAT, 0644, MAX_CLIENT);
    full = sem_open("/full", O_CREAT, 0644, 0);
}

void sigchld_handler(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    }
    if (pid < 0 && errno != ECHILD) {
        perror("waitpid");
        exit(EXIT_FAILURE);
    }
}

void send_response(int clientpid, resprotocol_t code, char* response_text){
    response_t response;
    snprintf(client_fifo,CLIENT_FIFO_NAME_LEN,CLIENT_FIFO_TEMPLATE,clientpid);
    //trying to open client fifo in writing mode
    int clientfd = open(client_fifo,O_WRONLY); //opeing in write only mode
    if(clientfd == -1){
        perror("server couldn't open clientfd");
        exit(EXIT_FAILURE);
    }

    //preparing response
    strncpy(response.body, response_text, strlen(response_text));
    response.body[strlen(response_text)] = '\0';
    response.rescode = code;

    //Writing to clientfd
    if(write(clientfd,&response,sizeof(response_t))!=sizeof(response_t)){
        perror("Couldn't write to client fifo");
        exit(EXIT_FAILURE);
    }

    //closing clientfd after sending response
    if(close(clientfd) == -1){
        perror("Couldn't close clientfd");
        exit(EXIT_FAILURE);
    }
}

void log_req(request_t req) {
    //checking if there is log file ornot
    int fd = open("server.log", O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd != -1) { 
        close(fd);  
    } else if (errno != EEXIST) {
        perror("Error opening file");
        return;
    }
    FILE *file = fopen("server.log", "a"); 
    if (file == NULL) {
        printf("Error opening log file!\n");
        return;
    }
    time_t current_time = time(NULL);
    char* c_time_string = ctime(&current_time);
    c_time_string[strcspn(c_time_string, "\n")] = 0;
    fprintf(file, "PID:%d  CMD_CODE:%d, TIME:%s\n", req.client_pid, req.action_code, c_time_string);
    fclose(file); 
}
//0 fail , 1 success
int producer(ProcessPool *pool, int pid) {
    sem_wait(mutex); // enter critical section
    int added = addProcess(pool, pid); // add item to the queue
    sem_post(mutex); // exit critical section
    return added;
}

//0 fail , 1 success
int consumer(ProcessPool* pool, int pid){
    sem_wait(mutex); // enter critical section
    int removed = removeProcess(pool, pid); // add item to the queue
    sem_post(mutex); // exit critical section
    return removed;
}


void init_sigchildhandler(){
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

void signal_handler(int sig){
    if(sig == SIGINT || sig ==SIGTERM){
        flag = 1;
    }
}

void init_signal(){
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

}




int main(int argc, char *argv[]) {
    
    int serverfd, dummyfd, clientfd;
    request_t request;
    response_t response;
    ProcessPool * processes;
    Queue * waitlist = createQueue();
    initProcessPool(&processes);
    init_semaphores();


    //setting signals
    init_sigchildhandler();
    signal(SIGPIPE, SIG_IGN); //Change

    //Creating well-known fifo
    umask(0); 
    server_pid = getpid(); //server id
    snprintf(server_fifo,SERVER_FIFO_NAME_LEN,SERVER_FIFO_TEMPLATE,getpid());//change it with serverpid later
    if ((mkfifo(server_fifo, 0644) == -1) && (errno!=EEXIST)) { //creating fifo
        perror("Error creating server FIFO");
        exit(EXIT_FAILURE);
    }
    printf(">> Server Started PID %d\n", server_pid);
    printf(">> waiting for clients...\n");
    // Open the server FIFO for reading
    serverfd = open(server_fifo, O_RDONLY);
    if (serverfd == -1) {
        perror("Error opening server FIFO");
        exit(EXIT_FAILURE);
    }
    //Opening a dummyfd so server doesn't see EOF
    dummyfd = open(server_fifo,O_WRONLY);
    if(dummyfd == -1){
        perror("DummyFd failed");
        exit(EXIT_FAILURE);
    }
    

    int isEligible = 1; //this flag will be true if a process is eligible
    // Main loop to handle client connections
    while (1) {
        
        //reading client request
        int reqread;
        while(((reqread = read(serverfd,&request,sizeof(request_t))) ==-1) && (errno == EINTR));
        if(reqread != sizeof(request_t)){
            perror("Couldn't read client request");
            exit(EXIT_FAILURE);
        }
        log_req(request);
        if(flag==1){
            //closign because of signal
            exit(EXIT_FAILURE);
        }
        //try connect don't add in process queue if full
        if(request.action_code == tryConnect){ 
            if(processes->size >= MAX_CLIENT){
                send_response(request.client_pid,connDeclined,"Counldn't Connect. Server Max Capacity");
                isEligible = 0;
            }else{
                //first time client
                if(findProcess(processes,request.client_pid) == -1){
                    printf("Client %d connected to server\n",request.client_pid);
                    send_response(request.client_pid,connEstablished,"Welcome to server");
                    producer(processes,request.client_pid);
                }
                isEligible = 1;
            }
        }else if(request.action_code == Connect){ //if processpool is full then add in queue
            if(processes->size >= MAX_CLIENT){
                send_response(request.client_pid,connWaiting,"Server Max Capacity. You are in waitlist");
                enqueue(waitlist,request.client_pid);
                isEligible = 0;
            }else{
                if(findProcess(processes,request.client_pid) == -1){
                    printf("Client %d connected to server\n",request.client_pid);
                    send_response(request.client_pid,connEstablished,"Welcome to server");
                    producer(processes,request.client_pid);
                }
                isEligible = 1;
            }
        }else{ //for already connected processes
            if(findProcess(processes,request.client_pid)==1){
                isEligible = 1;
            }
        }
        if(isEligible){
            int child_pid = fork();
            if(child_pid == 0){
                printf("Client PID %d : %d\n",request.client_pid,request.action_code);
                if(request.action_code == quit){//client done
                    int removed = consumer(processes,request.client_pid);
                    if(removed){
                        printf("Client %d disconnected...\n",request.client_pid);
                    }
                    exit(EXIT_SUCCESS);
                }
                
                else if(request.action_code == help){
                    send_response(request.client_pid,resComplete,"help, list, readF, writeF, upload <file> , download <file>, quit, killServer");
                }
                else if(request.action_code == readF){
                    Command cmd = parse_command(request.cmd);
                    execute_readF(cmd,request.client_pid);
                }else if (request.action_code == writeT){
                    Command cmd = parse_command(request.cmd);
                    execute_writeT(cmd,request.client_pid);
                }else if (request.action_code == list){
                    execute_list(request.client_pid);
                }else if (request.action_code == invalid){
                    send_response(request.client_pid,resComplete,"Invalid Request");
                }else if(request.action_code == upload){
                    Command cmd = parse_command(request.cmd);
                    execute_upload(cmd,request.client_pid);
                }else if (request.action_code == killServer){
                    for(int i = 0; i < processes->size; i++){
                        kill(processes->arr[i],SIGKILL);
                    }
                    printf("All Server Process Killed.Closing Server\n");
                    exit(EXIT_SUCCESS);
                }else if (request.action_code == download){
                    send_response(request.client_pid,resComplete,"You asked to download but not implemented");
                }
            }else if(child_pid <0){
                perror("fork");
                exit(EXIT_FAILURE);
            }   
        }
    }
    return 0;
}

void execute_readF(Command cmd,int clientpid){
    int line_size = 1024;
    int fd;
    struct flock fl;
    FILE* file;
    char line[line_size];
    int target_line = -1;  // for whole file by default

    // If a line number is given
    if (cmd.argc > 1)
        target_line = atoi(cmd.argv[1]);

    fd = open(cmd.argv[0], O_RDONLY);
    if (fd == -1) {
        perror("open");
        send_response(clientpid,resComplete,"No such file or directory");
        exit(EXIT_FAILURE);
    }

    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_RDLCK;
    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }

    file = fdopen(fd, "r");
    if (file == NULL) {
        perror("fdopen");
        exit(EXIT_FAILURE);
    }

    snprintf(client_fifo,CLIENT_FIFO_NAME_LEN,CLIENT_FIFO_TEMPLATE,clientpid);
    //trying to open client fifo in writing mode
    int clientfd = open(client_fifo,O_WRONLY); //opening in write only mode
    if(clientfd == -1){
        perror("server couldn't open clientfd");
        exit(EXIT_FAILURE);
    }
    response_t response;
    printf("target line: %d",target_line);
    if (target_line > 0) {
        for (int i = 1; i <= target_line; ++i) {
            if (fgets(line, line_size, file) == NULL) {
                perror("fgets");
                exit(EXIT_FAILURE);
            }
        }
        //send the response
        strncpy(response.body, line, strlen(line));
        response.body[strlen(line)] = '\0';
        response.rescode = resBuffer;

        if(write(clientfd,&response,sizeof(response_t))!=sizeof(response_t)){
            perror("Couldn't write to client fifo");
            exit(EXIT_FAILURE);
        }

    } else {
        int code = resBuffer;
        while (fgets(line, line_size, file) != NULL){
            //preparing response
            strncpy(response.body, line, strlen(line));
            response.body[strlen(line)] = '\0';
            response.rescode = resBuffer;

            //Writing to clientfd
            if(write(clientfd,&response,sizeof(response_t))!=sizeof(response_t)){
                perror("Couldn't write to client fifo");
                exit(EXIT_FAILURE);
            }
        }
        // Check if fgets stopped due to EOF or an error
        if (feof(file)) {
            // send EOF message
            strncpy(response.body, "EOF", 4);
            response.body[3] = '\0';
            response.rescode = resComplete;
            if(write(clientfd,&response,sizeof(response_t))!=sizeof(response_t)){
                perror("Couldn't write to client fifo");
                exit(EXIT_FAILURE);
            }
        } else if (ferror(file)) {
            // handle error
            perror("fgets");
            exit(EXIT_FAILURE);
        }
        // send_response(clientfd,resComplete,"EOF");
    }

    //closing clientfd after sending all responses
    if(close(clientfd) == -1){
        perror("Couldn't close clientfd");
        exit(EXIT_FAILURE);
    }

    fl.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }

    fclose(file);
}


void execute_writeT(Command cmd, int clientpid) {
    int line_size = 1024;
    int fd;
    struct flock fl;
    FILE *file;
    char line[line_size];
    int target_line = -1;  // for end of file by default
    char input_string[1024];
    // If a line number is given

    target_line = atoi(cmd.argv[1]);
    if((target_line==0) && strcmp(cmd.argv[1],"0")){
        int counter= 0;
        for(int i = 1; i < cmd.argc; i++){
            for(int j = 0; cmd.argv[i][j]!='\0';j++){
                input_string[counter++]=cmd.argv[i][j];
            }
            input_string[counter++]=' ';
        }
        input_string[counter]='\0';
    }else{//line given
        int counter= 0;
        for(int i = 2; i < cmd.argc; i++){
            for(int j = 0; cmd.argv[i][j]!='\0';j++){
                input_string[counter++]=cmd.argv[i][j];
            }
            input_string[counter++]=' ';
        }
        input_string[counter]='\0';


    }
       
        
    
    
    // Try to create the file exclusively.
    fd = open(cmd.argv[0], O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        // If the file already exists, open it normally.
        if (errno == EEXIST) {
            fd = open(cmd.argv[0], O_RDWR, S_IRUSR | S_IWUSR);
            if (fd == -1) {
                perror("open");
                exit(EXIT_FAILURE);
            }
        } else {
            perror("open");
            exit(EXIT_FAILURE);
        }
    }

    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;
    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }

    file = fdopen(fd, "r+");
    if (file == NULL) {
        perror("fdopen");
        exit(EXIT_FAILURE);
    }

    if (target_line > 0) {
        for (int i = 1; i < target_line; ++i) {
            if (fgets(line, line_size, file) == NULL) {
                // If the file has fewer lines than the target, append at the end.
                fseek(file, 0, SEEK_END);
                break;
            }
        }
    } else {
        // If no target line is specified, write to the end of the file.
        fseek(file, 0, SEEK_END);
    }
    
    fputs(input_string, file);
    fputc('\n', file);

    fl.l_type = F_UNLCK;
    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }
    send_response(clientpid,resComplete,"Done Writing");

    fclose(file);
}


void execute_list(int clientpid){
    DIR *dir;
    struct dirent *entry;

    dir = opendir(".");
    if (!dir) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    char client_fifo[256];
    snprintf(client_fifo, sizeof(client_fifo), CLIENT_FIFO_TEMPLATE, clientpid);

    int clientfd = open(client_fifo, O_WRONLY);
    if (clientfd == -1) {
        perror("Couldn't open client FIFO");
        exit(EXIT_FAILURE);
    }

    response_t response;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            strncpy(response.body, entry->d_name, sizeof(response.body));
            response.rescode = resBuffer;
            if (write(clientfd, &response, sizeof(response_t)) != sizeof(response_t)) {
                perror("Couldn't write to client FIFO");
                exit(EXIT_FAILURE);
            }
        }
    }

    // Send EOF message
    strncpy(response.body, "EOF", 4);
    response.body[3] = '\0';
    response.rescode = resComplete;

    if (write(clientfd, &response, sizeof(response_t)) != sizeof(response_t)) {
        perror("Couldn't write to client FIFO");
        exit(EXIT_FAILURE);
    }

    if (close(clientfd) == -1) {
        perror("Couldn't close client FIFO");
        exit(EXIT_FAILURE);
    }

    if (closedir(dir) == -1) {
        perror("Couldn't close directory");
        exit(EXIT_FAILURE);
    }
}

void execute_upload(Command cmd,int clientpid){
    //checking if filename in server already
    int tofd = open(cmd.argv[0],O_WRONLY);
    if(tofd !=-1){
        send_response(clientpid,resComplete,"file with same name exists in server");
        exit(EXIT_FAILURE);
    }
    //finame in cmd[0]
    printf("filename:%s\n",cmd.argv[0]);
    char clientfilepath[100];
    strcpy(clientfilepath,"../Client/");
    int count = strlen(clientfilepath);
    for(int i = 0; cmd.argv[0][i]!='\0'; i++){
        clientfilepath[count++] = cmd.argv[0][i];
    } 
    clientfilepath[count]='\0';
    printf("server file path:%s\n",clientfilepath);
    int fromfd = open(clientfilepath,O_RDONLY);
    if(fromfd == -1){
        perror("open");
        send_response(clientpid,resComplete,"file not found");
        exit(EXIT_FAILURE);
    }


    send_response(clientpid,resComplete,"you asked for file upload");

}
