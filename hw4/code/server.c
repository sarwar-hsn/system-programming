#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include "app.h"
#include "protocols.h"
#include "conn_clients.h"
#include "jobqueue.h"

volatile sig_atomic_t server_running = 1;

// Function prototypes
void cleanup();
void execute_readF(Command,int);
void execute_writeT(Command,int);
void execute_list(int);
void execute_upload(Command,int);
void execute_download(Command,int);
int copy_file(int,int);

// Global variables
char serverdir[100];
int server_pid;
char server_fifo[SERVER_FIFO_NAME_LEN];
char client_fifo[CLIENT_FIFO_NAME_LEN];
int current_clients = 0;
int active_readers = 0; 
int active_writers = 0;
int waiting_writers = 0; 
int serverfd, dummyfd, clientfd;

JobQueue job_queue; 
Connclients* clients;

//mutexes
pthread_mutex_t mutex_jqueue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_clients = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_rw = PTHREAD_MUTEX_INITIALIZER;


//condition variables
pthread_cond_t cv_emptyq = PTHREAD_COND_INITIALIZER;
pthread_cond_t can_read = PTHREAD_COND_INITIALIZER; 
pthread_cond_t can_write = PTHREAD_COND_INITIALIZER;

//removed client fifo after using it
void removeFifo(void){
    unlink(client_fifo);
    unlink(server_fifo);
}




void send_response(int clientpid, resprotocol_t code, char* response_text){
    response_t response;
    snprintf(client_fifo,CLIENT_FIFO_NAME_LEN,CLIENT_FIFO_TEMPLATE_FOR_SERVER,clientpid);
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

void cleanup(){
    unlink(server_fifo);
    free_connclients(clients);
    destroy_jqueue(&job_queue);
    exit(EXIT_SUCCESS);
}

void* worker_function(){
     while (1) {
        pthread_mutex_lock(&mutex_jqueue);
        while( job_queue.size == 0){
            pthread_cond_wait(&cv_emptyq, &mutex_jqueue);
        }

        // Fetch a job and reduce the queue size.
        Job job = dequeue_jobs(&job_queue);
        pthread_mutex_unlock(&mutex_jqueue);

        // Process the job.
        if(job.req.action_code == help){
            send_response(job.req.client_pid,resComplete,"help, list, readF, writeF, upload <file> , download <file>, quit, killServer");
        }else if(job.req.action_code == readF){
            Command cmd = parse_command(job.req.cmd);
            execute_readF(cmd,job.req.client_pid);
        }else if(job.req.action_code == writeT){
            Command cmd = parse_command(job.req.cmd);
            execute_writeT(cmd,job.req.client_pid);
        }else if (job.req.action_code == invalid){
            send_response(job.req.client_pid,resComplete,"Invalid Request");
        }else if(job.req.action_code == quit){
            //send_response(job.req.client_pid,resBuffer,"preparing log file");
            prepare_client_logfile(job.req.client_pid);
            remove_connclient(clients,job.req.client_pid);
            printf("Client %d disconnected\n",job.req.client_pid);
            send_response(job.req.client_pid,connClosed,"Log file created. Bye....");
        }else if (job.req.action_code == upload){
            Command cmd = parse_command(job.req.cmd);
            execute_upload(cmd,job.req.client_pid);
        }else if (job.req.action_code == download){
            Command cmd = parse_command(job.req.cmd);
            execute_download(cmd,job.req.client_pid);
        }else if (job.req.action_code == list){
            execute_list(job.req.client_pid);
        }else if (job.req.action_code == killServer){
            for(int i =0; i < clients->size; i++){
                send_response(clients->array[i],connClosed,"terminate");
            }
            exit(EXIT_SUCCESS);
        }
    }
    return NULL;

}

void sigint_handler(int sig) {
    if(sig == SIGINT )
        server_running = 0;
    cleanup();
}

int main(int argc, char *argv[]) {
    int max_client, pool_size; 
    request_t request;

    if (argc < 3){
        fprintf(stderr,"Usage: <dirname> <max. #ofClients> <poolSize>");
        exit(EXIT_FAILURE);
    }else{
        strcpy(serverdir,argv[1]);
        max_client = atoi(argv[2]);
        pool_size = atoi(argv[3]);
        printf("serverdir: %s # of clients: %d poolsize: %d\n", serverdir,max_client,pool_size );
    }
    //writing server config
    struct server_config config;
    config.serverpid=getpid();
    strcpy(config.serverdir,serverdir);
    write_server_config(&config);

    //Initializations after command line arguments
    pthread_t thread_pool[pool_size];
    clients = init_connclients(max_client); // a hash data structure would be better fit in this place
    init_jqueue(&job_queue);

    //creating server directory if doesn't exists
    if(mkdir(serverdir,0777)==-1 && errno!=EEXIST){
        perror("mkdir server");
        exit(EXIT_FAILURE);
    }
    //entering serverdir
    if(chdir(serverdir)==-1){
        perror("chdir server");
        exit(EXIT_FAILURE);
    }

    printf("serverdir: %s, serverpid: %d\n",config.serverdir,config.serverpid);
    //Creating well-known fifo
    umask(0); 
    snprintf(server_fifo,SERVER_FIFO_NAME_LEN, SERVER_FIFO_TEMPLATE, config.serverpid); //preparing template
    if ((mkfifo(server_fifo, 0644) == -1) && (errno != EEXIST)) {
        fprintf(stderr, "Error creating server FIFO '%s': %s\n", server_fifo, strerror(errno));
        exit(EXIT_FAILURE);
    }

    

    // Register signal handler for SIGPIPE
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    sa.sa_handler = &sigint_handler;
    sa.sa_flags = SA_RESTART; 
    sigfillset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error cannot handle SIGINT");
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("Error cannot handle SIGINT");
    }

    //opening main server
    printf(">> Server Started PID %d\n", config.serverpid);
    printf(">> waiting for clients...\n\n");

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

    

    for (int i = 0; i < pool_size; i++) {
        if(pthread_create(&thread_pool[i], NULL, &worker_function, NULL)>0){
            perror("pthread create");
            exit(EXIT_FAILURE);
        }
    }

    //removing fifo atexit 
    if (atexit(removeFifo) != 0){
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    //main server loop
    int isEligible = 1;
    while (server_running) {
        //reading client request
        if(!server_running){
            close(serverfd);
        }
        int reqread;
        reqread = read(serverfd,&request,sizeof(request_t));
        if(reqread != sizeof(request_t)){
            perror("Couldn't read client request");
            exit(EXIT_FAILURE);
        }
        write_server_log(request);
        //if request is try connect then at first we check queue 
        if (request.action_code == tryConnect || request.action_code == Connect){
            if(clients->size >= max_client){
                send_response(request.client_pid,connDeclined,"Counldn't Connect. Server Max Capacity");
                isEligible = 0;
            }else{
                if(find_connclient(clients,request.client_pid) ==-1){ // if not found in clients
                    printf("Client %d connected to server\n",request.client_pid);
                    send_response(request.client_pid,connEstablished,"Welcome to server");
                    add_connclient(clients,request.client_pid);
                }
                isEligible = 1;
            }
        }else{
            if(find_connclient(clients,request.client_pid) != -1){
                isEligible = 1;
            }else{
                isEligible =0;
            }
        }
        if(isEligible){
            //adding the request to job queue
            Job temp;
            temp.req = request;
            pthread_mutex_lock(&mutex_jqueue);
            enqueue_jobs(&job_queue,temp);
            printf("pid:%d cmd:%d\n",temp.req.client_pid,temp.req.action_code);
            pthread_cond_signal(&cv_emptyq); //telling one sleeping threads wake up to handle this request
            pthread_mutex_unlock(&mutex_jqueue);
        }
    }

    for(int i =0; i < pool_size; i++){
        if(pthread_join(thread_pool[i], NULL)>0){
            perror("pthread create");
            exit(EXIT_FAILURE);
        }
    }
    close(serverfd);
    close(dummyfd);
    unlink(server_fifo);
    return 0;
}


void execute_readF(Command cmd,int clientpid){
    pthread_mutex_lock(&mutex_rw);

    // Wait until there is no active or waiting writers.
    while (active_writers > 0 || waiting_writers > 0) {
        pthread_cond_wait(&can_read, &mutex_rw);
    }
    ++active_readers; // Increment count of active readers.
    pthread_mutex_unlock(&mutex_rw);


    //read the file .... writers are already locked
    int fd, line_size = 1024;
    FILE* file;
    char line[line_size];
    int target_line = -1;  // for whole file by default
    if (cmd.argc > 1)
        target_line = atoi(cmd.argv[1]);
    //opening filedescriptor
    fd = open(cmd.argv[0], O_RDONLY);
    if (fd == -1) {
        perror("open");
        send_response(clientpid,resComplete,"No such file or directory");
        exit(EXIT_FAILURE);
    }
    file = fdopen(fd, "r");
    if (file == NULL) {
        perror("fdopen");
        exit(EXIT_FAILURE);
    }
    snprintf(client_fifo,CLIENT_FIFO_NAME_LEN,CLIENT_FIFO_TEMPLATE_FOR_SERVER,clientpid);
    int clientfd = open(client_fifo,O_WRONLY); //opening in write only mode
    if(clientfd == -1){
        send_response(clientpid,resFail,"something went wrong with server");
        return;
    }
    response_t response;
    if (target_line > 0) {
        for (int i = 1; i <= target_line; ++i) {
            if (fgets(line, line_size, file) == NULL) {
                send_response(clientpid,resFail,"something went wrong with server");
                return;
            }
        }
        //send the response
        strncpy(response.body, line, strlen(line));
        response.body[strlen(line)] = '\0';
        response.rescode = resBuffer;

        if(write(clientfd,&response,sizeof(response_t))!=sizeof(response_t)){
            send_response(clientpid,resFail,"something went wrong with server");
            return;
        }

    } else {
        while (fgets(line, line_size, file) != NULL){
            //preparing response
            strncpy(response.body, line, strlen(line));
            response.body[strlen(line)] = '\0';
            response.rescode = resBuffer;

            //Writing to clientfd
            if(write(clientfd,&response,sizeof(response_t))!=sizeof(response_t)){
                send_response(clientpid,resFail,"something went wrong with server");
                return;
            }
        }
        // Check if fgets stopped due to EOF or an error
        if (feof(file)) {
            // send EOF message
            strncpy(response.body, "EOF", 4);
            response.body[3] = '\0';
            response.rescode = resComplete;
            if(write(clientfd,&response,sizeof(response_t))!=sizeof(response_t)){
                send_response(clientpid,resFail,"something went wrong with server");
                return;
            }
        } else if (ferror(file)) {
            // handle error
            send_response(clientpid,resFail,"something went wrong with server");
            return;
        }
    }
    if(close(clientfd) == -1){
        send_response(clientpid,resFail,"something went wrong with server");
        return;
    }
    fclose(file);

    //decreasing reader count
    pthread_mutex_lock(&mutex_rw);
    --active_readers; // Decrement count of active readers.
    if (active_readers == 0) {   // If no active readers, notify the waiting writers.
        pthread_cond_signal(&can_write);
    }
    pthread_mutex_unlock(&mutex_rw);

}

void execute_writeT(Command cmd,int clientpid){
    pthread_mutex_lock(&mutex_rw);
    ++waiting_writers; // Increment count of waiting writers.
    while (active_readers > 0 || active_writers > 0) {
        pthread_cond_wait(&can_write, &mutex_rw);
    }
    --waiting_writers; // Decrement count of waiting writers.
    ++active_writers; // Increment count of active writers.
    pthread_mutex_unlock(&mutex_rw);

    //writing part
    int fd, line_size = 1024;
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

    file = fdopen(fd, "r+");
    if (file == NULL) {
        perror("fdopen");
        exit(EXIT_FAILURE);
    }
    send_response(clientpid,resBuffer,"Writing....");
    if (target_line > 0) {
        char **lines = NULL;
        int num_lines = 0;
        while (fgets(line, line_size, file) != NULL) {
            lines = realloc(lines, sizeof(char *) * (num_lines + 1));
            if (lines == NULL) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            lines[num_lines] = strdup(line);
            if (lines[num_lines] == NULL) {
                perror("strdup");
                exit(EXIT_FAILURE);
            }
            ++num_lines;
        }

        fclose(file);

        // Append to the specific line.
        if (target_line > 0 && target_line <= num_lines) {
            // Resize the target line to fit the new text.
            int old_length = strlen(lines[target_line - 1]);
            int new_length = old_length + strlen(input_string) + 1;
            lines[target_line - 1] = realloc(lines[target_line - 1], new_length);
            if (lines[target_line - 1] == NULL) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            strcat(lines[target_line - 1], input_string); // Append the new text.
        } else {
            // If the target line is beyond the end of the file, just append a new line.
            lines = realloc(lines, sizeof(char *) * (num_lines + 1));
            if (lines == NULL) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            lines[num_lines] = strdup(input_string);
            if (lines[num_lines] == NULL) {
                perror("strdup");
                exit(EXIT_FAILURE);
            }
            ++num_lines;
        }

        // Write all lines back to the file.
        fd = open(cmd.argv[0], O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            perror("open");
            exit(EXIT_FAILURE);
        }

        file = fdopen(fd, "w");
        if (file == NULL) {
            perror("fdopen");
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < num_lines; ++i) {
            fputs(lines[i], file);
        }

        fclose(file);
        close(fd);

        // Free the memory.
        for (int i = 0; i < num_lines; ++i) {
            free(lines[i]);
        }
        free(lines);
    } else {
        fseek(file, 0, SEEK_END); // If no target line is specified, write to the end of the file.
    }
    
    fputs(input_string, file);
    fputc('\n', file);

    fclose(file);
    close(fd);
    send_response(clientpid,resComplete,"Done Writing");

    //decreasing active writer & signaling readers
    pthread_mutex_lock(&mutex_rw);
    --active_writers; // Decrement count of active writers.
    if (waiting_writers > 0) {
        pthread_cond_signal(&can_write);
    } else {
        pthread_cond_broadcast(&can_read);
    }
    pthread_mutex_unlock(&mutex_rw);

}

int copy_file(int fromfd, int tofd){
    char buf[BUFSIZE];
    int bytesread, byteswritten;
    int totalbytes = 0;

    // Create file locks
    struct flock fl_read = {F_RDLCK, SEEK_SET, 0, 0, 0};
    struct flock fl_write = {F_WRLCK, SEEK_SET, 0, 0, 0};

    while ((bytesread = read(fromfd, buf, BUFSIZE)) > 0) {
        char *bp = buf;

        // Lock the file for reading
        if (fcntl(fromfd, F_SETLKW, &fl_read) == -1) {
            return -1;
        }

        while (bytesread > 0) {
            // Lock the file for writing
            if (fcntl(tofd, F_SETLKW, &fl_write) == -1) {
                return -1;
            }

            byteswritten = write(tofd, bp, bytesread);
            if (byteswritten <= 0) {
                if (byteswritten == -1) perror("write error");
                return -1;
            }

            // Unlock the write lock
            fl_write.l_type = F_UNLCK;
            if (fcntl(tofd, F_SETLK, &fl_write) == -1) {
                perror("unlock write failed");
                return -1;
            }

            totalbytes += byteswritten;
            bytesread -= byteswritten;
            bp += byteswritten;
        }

        // Unlock the read lock
        fl_read.l_type = F_UNLCK;
        if (fcntl(fromfd, F_SETLK, &fl_read) == -1) {
            perror("unlock read failed");
            return -1;
        }
    }

    if (bytesread == -1) perror("read error");

    return totalbytes;
}

void execute_upload(Command cmd, int clientpid) {
    //copyfile here
    int fromfd, tofd;
    char filename[200];
    char new_filename[200];
    sprintf(filename, CLIENT_DIR_FILE_FORMAT, cmd.argv[0]);
    fromfd = open(filename, O_RDONLY); //working directory of client
    if (fromfd == -1) {
        send_response(clientpid, resFail, "file not found");
        return;
    }
    strcpy(new_filename, cmd.argv[0]);
    tofd = open(new_filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (tofd == -1 && errno == EEXIST) {
        int i = 1;
        do {
            // Try to open a file with a _copy(i) suffix.
            sprintf(new_filename, "%s_copy(%d)", cmd.argv[0], i);
            tofd = open(new_filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
            i++;
        } while (tofd == -1 && errno == EEXIST);
        if (tofd == -1) {
            // If we still couldn't open a file, report an error.
            send_response(clientpid, resFail, "error! couldn't create a new file");
            close(fromfd);  // Close file descriptor
            return;
        }
    }
    copy_file(fromfd, tofd);
    close(fromfd);  // Close file descriptor
    close(tofd);    // Close file descriptor
    send_response(clientpid, resComplete, "File Uploaded to Server");
}

void execute_download(Command cmd, int clientpid) {
    int fromfd, tofd;
    fromfd = open(cmd.argv[0], O_RDONLY);
    if (fromfd == -1) {
        send_response(clientpid, resFail, "error! couldn't find this file in server");
        return;
    }

    char filename[200];
    char new_filename[300];
    sprintf(filename, CLIENT_DIR_FILE_FORMAT, cmd.argv[0]);
    strcpy(new_filename, filename);
    tofd = open(new_filename, O_WRONLY | O_CREAT | O_EXCL, 0666); //working directory of client
    if (tofd == -1 && errno == EEXIST) {
        int i = 1;
        do {
            // Try to open a file with a _copy(i) suffix.
            sprintf(new_filename, "%s_copy(%d)", filename, i);
            tofd = open(new_filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
            i++;
        } while (tofd == -1 && errno == EEXIST);
        if (tofd == -1) {
            // If we still couldn't open a file, report an error.
            close(fromfd);  // Close file descriptor
            send_response(clientpid, resFail, "error! couldn't create a new file in client dir");
            return;
        }
    }

    snprintf(client_fifo,CLIENT_FIFO_NAME_LEN,CLIENT_FIFO_TEMPLATE_FOR_SERVER,clientpid);
    //trying to open client fifo in writing mode
    int clientfd = open(client_fifo,O_WRONLY); //opening in write only mode
    if(clientfd == -1){
        close(fromfd);  // Close file descriptor
        close(tofd);    // Close file descriptor
        send_response(clientpid,resFail, "server couldn't open clientfd");
        return;
    }
    copy_file(fromfd, tofd);
    close(fromfd);  // Close file descriptor
    close(tofd);    // Close file descriptor
    send_response(clientpid, resComplete, "File Downloaded from Server");
    return;
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
    snprintf(client_fifo, sizeof(client_fifo), CLIENT_FIFO_TEMPLATE_FOR_SERVER, clientpid);

    int clientfd = open(client_fifo, O_WRONLY);
    if (clientfd == -1) {
        send_response(clientpid,resComplete,"something went wrong");
        return;
    }

    response_t response;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            strncpy(response.body, entry->d_name, sizeof(response.body));
            response.rescode = resBuffer;
            if (write(clientfd, &response, sizeof(response_t)) != sizeof(response_t)) {
                perror("Couldn't write to client FIFO");
                send_response(clientpid,resComplete,"something went wrong");
                return;
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