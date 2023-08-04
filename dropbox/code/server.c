#include "common.h"
#include "clientqueue.h"
#include "filequeue.h"
#include "hashtable.h"

#define SERVER_BACKLOG 10

volatile sig_atomic_t signal_flag = 0;

//global variables
char server_path[100];
clientqueue_t connected_clients;
int poolsize;

int conn_client_count = 0;


//mutexes
pthread_mutex_t critrgn_mutex = PTHREAD_MUTEX_INITIALIZER;//client queue mutex
pthread_mutex_t cq_mutex = PTHREAD_MUTEX_INITIALIZER;//client queue mutex
pthread_mutex_t ft_mutex = PTHREAD_MUTEX_INITIALIZER;//client queue mutex

//condition variables
pthread_cond_t cq_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t cq_full = PTHREAD_COND_INITIALIZER;



//function definitions
void* worker_function();
void handle_client(int);
void print_server_info(char*,int);
int send_status_to_client(int client_sock, protocol_t code);
int send_data_to_client(int client_sock,protocol_t code, char* message);
void send_dir_to_client(int client_sock,const char* src_dir_path,filequeue_t* filesendqueue,hashtable_t* fileinfos );
void send_files_to_client(filequeue_t* filesendqueue);
void receive_dir_from_client(int client_sock,hashtable_t* fileinfos);
void receive_files_from_client(int client_sock,hashtable_t* fileinfos);
int read_file_from_client(int fromfd,int tofd);
void read_file_to_client(int fromfd,int tofd);
void initial_dir_info(const char* src_dir_path,hashtable_t* fileinfos);
void insepect_changes(const char* src_dir_path,hashtable_t* fileinfos,int * dir_file_send_flag);
void* signal_thread();


void* signal_thread(){
    sigset_t set;
    int signal;
    // initialize the signal set
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    // loop to handle signals
    while(1){
        sigwait(&set, &signal);
        if(signal == SIGINT || signal == SIGTERM){
            printf("Please wait closing server....\n");
            signal_flag = 1;
            // waking up threads blocked on condition variables
            pthread_mutex_lock(&cq_mutex);
            pthread_cond_broadcast(&cq_full);
            pthread_cond_broadcast(&cq_empty);
            pthread_mutex_unlock(&cq_mutex);
            break; // stop after receiving a signal
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {

    //creating server directory
    int port;
    char* ip_address;
    char* server_dir_name;
    if(argc < 4) {
        printf("Usage: %s <dirName> <threadpoolsize> <portnumber> [optional - ip_address (default - 127.0.0.1)]\n", argv[0]);
        return 1;
    }

    server_dir_name = argv[1];
    poolsize = atoi(argv[2]);
    port = atoi(argv[3]);

    if(argc >= 5) {
        ip_address = argv[4];
    } else {
        ip_address = "127.0.0.1"; //default value
    }

    struct sockaddr_in server_addr, client_addr;
    int serversock, addr_size;
    pthread_t threadpool[poolsize];

     //setting signaling thread
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    pthread_t sig_thread;
    if(pthread_create(&sig_thread, NULL, signal_thread, NULL) != 0){
        perror("signal thread");
        exit(EXIT_FAILURE);
    }

    snprintf(server_path,SERVER_TEMPLATE_LEN,SERVER_TEMPLATE,server_dir_name);
    int is_dir_created = mkdir(server_path,0766);
    if((is_dir_created == -1) && (errno != EEXIST)){
        perror("server root dir error");
        exit(EXIT_FAILURE);
    }

    if(chdir(server_path)==-1){
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < poolsize; i++){
        if(pthread_create(&threadpool[i],NULL,worker_function,NULL)>0){
            perror("worker thread");
            exit(EXIT_FAILURE);
        }
    }

    init_clientqueue(&connected_clients);

    serversock = socket(AF_INET, SOCK_STREAM, 0);
    if (serversock == -1) {
        perror("server socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip_address, &(server_addr.sin_addr)) <= 0) {
        perror("inet_pton");
        return EXIT_FAILURE;
    }
    server_addr.sin_port = htons(port); // use variable port

    if (bind(serversock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("server bind");
        return EXIT_FAILURE;
    }

    if (listen(serversock, SERVER_BACKLOG) == -1) {
        perror("listen");
        return EXIT_FAILURE;
    }
    
    print_server_info(ip_address,port);
    struct timeval timeout;
    timeout.tv_sec = 1;  // After 1 second
    timeout.tv_usec = 0;

    if (setsockopt(serversock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("setsockopt failed\n");
    }
    if (setsockopt(serversock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        perror("setsockopt failed\n");
    }

    while (!signal_flag) {
        addr_size = sizeof(client_addr);
        int client_sock = accept(serversock, (struct sockaddr*)&client_addr, (socklen_t*)&addr_size);
        if (client_sock == -1) {
        if ((errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) && signal_flag) {
            printf("Shutting down...\n");
                break;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;  // Timeout, go back and check again
            } else {
                perror("server accept");
                break;
            }
        }
        printf("client %s connected\n", inet_ntoa(client_addr.sin_addr));

        pthread_mutex_lock(&cq_mutex);
        while(conn_client_count == MAX_QUEUE_SIZE){
            pthread_cond_wait(&cq_empty,&cq_mutex);
        }
        int isEnqueued = enqueue_client(&connected_clients,client_sock);
        if(isEnqueued!=-1){
            conn_client_count+=1;
            pthread_cond_signal(&cq_full);
        }
        pthread_mutex_unlock(&cq_mutex);
    }

    for(int i = 0; i < poolsize; i++) {
        if(pthread_join(threadpool[i], NULL) != 0){
            perror("Failed to join thread");
        }
    }

    printf("cleaning resources\n");
    pthread_mutex_destroy(&critrgn_mutex);
    pthread_mutex_destroy(&cq_mutex);
    pthread_mutex_destroy(&ft_mutex);

    close(serversock);

    return 0;

}

void handle_client(int client_sock) {
    request_t client_request;
    int need_sync = 0;
    filequeue_t filesendqueue;
    hashtable_t* fileinfos;
    fileinfos = hashtable_create(MAX_TABLE_SIZE);
    if(!fileinfos){
        fprintf(stderr,"failed to create hashtable\n");
        return;
    }
    init_filequeue(&filesendqueue);
    
    char client_dir_path[MAX_PATH_LENGTH];
    //telling client it's connected
    send_status_to_client(client_sock,connected);
    
    //recieve client root directory
    recv(client_sock,&client_request,sizeof(client_request),0);
    client_request.data[strlen(client_request.data)]='\0';
    strcpy(client_dir_path,client_request.data);
    printf("Client Root Dir - %s\n",client_dir_path);

    //start sending server files
    int root_dir_exists = mkdir(client_dir_path,0766);
    if(root_dir_exists ==-1){ 
        if(errno != EEXIST){
            perror("client root dir error");
            return;
        }
        need_sync = 1;
    }else{//nothing to send to client
        send_status_to_client(client_sock,sync_done);
        need_sync = 0;
    }
    initial_dir_info(client_dir_path,fileinfos);
    if(need_sync){
        //if sync need sending sync start
        send_status_to_client(client_sock,sync_start);
        send_dir_to_client(client_sock,client_dir_path,&filesendqueue,fileinfos);

        send_data_to_client(client_sock,sync_done,"done");//sending dir
        
        send_files_to_client(&filesendqueue);
        
        response_t syncdoneres;
        strcpy(syncdoneres.data,"all files sent");
        syncdoneres.code = sync_done;
        write(client_sock,&syncdoneres,sizeof(syncdoneres));
    }
    //client will send signal if it will send any files
    receive_dir_from_client(client_sock,fileinfos);
    receive_files_from_client(client_sock,fileinfos);
    printf("initial sync complete\n");

    int dir_file_send_flag = 0;
    //setting timeout
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        perror("setsockopt failed");
        return;
    }
    while (1) {
        if(signal_flag == 1){
            hashtable_free(fileinfos);
            pthread_exit(0);
        }
        printf("waiting for client data\n");
        protocol_t req_code;
        ssize_t bytes_received = recv(client_sock,&req_code, sizeof(int), 0);
        if (bytes_received <= 0) {
            if (bytes_received == 0) {
                printf("Client closed the connection\n");
                break;
            }
        }else if (bytes_received>0){
            if(req_code == sync_start){
                printf("receiving file/dir from client\n");
                receive_dir_from_client(client_sock,fileinfos);
                receive_files_from_client(client_sock,fileinfos);
            }
        }
        if(dir_file_send_flag!=1){ //don't make it 0 the could be files to send
            dir_file_send_flag = 0;
            insepect_changes(client_dir_path,fileinfos,&dir_file_send_flag);
        }

        if(dir_file_send_flag == 1){
            send_status_to_client(client_sock,sync_start);
            protocol_t final_response; //to check if client is ready
            recv(client_sock,&final_response,sizeof(int),0);
            if(final_response == sync_start){
                send_status_to_client(client_sock,sync_start);
                send_dir_to_client(client_sock,client_dir_path,&filesendqueue,fileinfos);
                send_data_to_client(client_sock,sync_done,"done");//sending dir
                send_files_to_client(&filesendqueue);
                response_t syncdoneres;
                strcpy(syncdoneres.data,"all files sent");
                syncdoneres.code = sync_done;
                write(client_sock,&syncdoneres,sizeof(syncdoneres));
                dir_file_send_flag = 0;
            }
            else if(final_response == sync_wait){
                send_status_to_client(client_sock,sync_done);
            }
        }
    }
}


void print_server_info(char* ip,int port){
    printf("\t\t***server started***\n");
    printf("server address:%s:%d\n", ip, port);
    printf("backlog size  :%d\n",SERVER_BACKLOG);
    printf("no. of thread :%d\n",poolsize);
    printf("queue capacity:%d\n",MAX_QUEUE_SIZE);
    printf("\nWaiting for connection...\n\n");
}

int send_status_to_client(int client_sock, protocol_t code){
    int code_to_send = code;
    int bytes_sent = send(client_sock, &code_to_send, sizeof(int), 0);
    return bytes_sent;
}

int send_data_to_client(int client_sock,protocol_t code, char* message){
    response_t response;
    strcpy(response.data, message);
    response.data[strlen(response.data)]='\0';
    response.code = code;
    int bytes_written = write(client_sock,&response,sizeof(response));
    return bytes_written;
}

void send_dir_to_client(int client_sock,const char* src_dir_path,filequeue_t* filesendqueue,hashtable_t* fileinfos ){
    DIR* dir = opendir(src_dir_path); //opening source directory
    if (dir == NULL) {
        fprintf(stderr,"Couldn't open source directory:%s\n",src_dir_path);
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        //cq_full source path
        char src_path[MAX_PATH_LENGTH];
        snprintf(src_path, MAX_PATH_LENGTH, "%s/%s", src_dir_path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            fileinfo_t* file = hashtable_get(fileinfos,src_path);
            if(file!=NULL && file->is_updated == 0){//is not synced with client. need to send
                file->is_updated = 1;
                protocol_t response_code;
                char path[MAX_PATH_LENGTH];
                strcpy(path,src_path);
                send_data_to_client(client_sock,dir_type,path);
                recv(client_sock,&response_code,sizeof(response_code),0);
                if(response_code == fail){
                    printf("directory %s creation failed in client\n",entry->d_name);
                }
            }
            send_dir_to_client(client_sock, src_path,filesendqueue,fileinfos);//recursive call
        }else if(entry->d_type == DT_REG){
            fileinfo_t* file = hashtable_get(fileinfos,src_path);
            if(file!=NULL){
                if(file->is_updated == 0){ //file marked to be sent
                    task_t task; 
                    strcpy(task.filename,src_path);
                    task.client_sock = client_sock;
                    enqueue_task(filesendqueue,task);
                    file->is_updated = 1;
                }
            }
        }
    }
    closedir(dir);
}

//reading from source and writing to clientsocket
void read_file_to_client(int fromfd,int tofd){
    int buffer_length = 1024;
    char buffer[buffer_length];
    int bytes;
    //int byteswritten = 0;
    while ((bytes = read(fromfd, buffer, buffer_length - 1)) > 0) {
        buffer[bytes] = '\0';
        if (write(tofd, buffer, bytes) != bytes) {
            break;
        } 
        //receiveing acknowledgement before next read
        int amount_received;
        recv(tofd,&amount_received,sizeof(int),0);
        printf("from client:%d received in server:%d writing to socket:%d\n",bytes,amount_received,tofd);
    }
    write(tofd,"EOF",3);
}

void send_files_to_client(filequeue_t* filesendqueue) {
    while(!is_filequeue_empty(filesendqueue)) {
        task_t task = dequeue_task(filesendqueue);
        if(task.client_sock !=-1){
            int src_fd = open(task.filename,O_RDONLY);
            if(src_fd != -1){
                response_t filefd_res;
                strcpy(filefd_res.data,task.filename);
                filefd_res.code = buff_start; //sending buffer
                write(task.client_sock,&filefd_res,sizeof(filefd_res));
                
                //now wait for response from client
                protocol_t clientfd_status_res;
                read(task.client_sock,&clientfd_status_res,sizeof(int));
                if(clientfd_status_res == succ){
                    printf("sending to client %s\n",task.filename);
                    read_file_to_client(src_fd,task.client_sock);
                }
            }
        }
    }
}

void receive_dir_from_client(int client_sock,hashtable_t* fileinfos){
    request_t client_req;
    while (1){
        recv(client_sock,&client_req,sizeof(client_req),0);
        if(client_req.code == dir_type){
            char path[MAX_PATH_LENGTH];
            client_req.data[strlen(client_req.data)]='\0';
            strcpy(path,client_req.data);

            int is_dir_created = mkdir(path,0766);
            if(is_dir_created!=-1){//created new directory sent by client
                time_t current_time;
                time(&current_time);
                fileinfo_t file;
                strcpy(file.filename,path);
                file.is_updated = 1;
                file.last_mod_time = current_time;
                file.is_dir = 1;
                hashtable_insert(fileinfos,path,file);
                send_status_to_client(client_sock,succ);
            }else if(is_dir_created == -1){ //directory exists
                if(errno == EEXIST){
                    fileinfo_t* file = hashtable_get(fileinfos,path);
                    if(file!=NULL){
                        time_t current_time;
                        time(&current_time);
                        file->is_updated = 1;
                        file->is_dir = 1;
                        // file->last_mod_time = current_time;
                    }
                    send_status_to_client(client_sock,succ);
                }else{
                    send_status_to_client(client_sock,fail);
                }
            }
        }else if((client_req.code == sync_done )){
            printf("client-to-server directory sync done\n");
            break;
        }
    }
}

int read_file_from_client(int fromfd,int tofd){
    int buffer_length = 1024;
    char buffer[buffer_length];
    int bytes;
    ssize_t byteswritten = 0;
    while ((bytes = read(fromfd, buffer, buffer_length-1)) > 0) {
        buffer[bytes]='\0';
        if(strcmp(buffer,"EOF")==0){
            break;
        }
        send_status_to_client(fromfd,bytes); //sending acknowledgement
        write(tofd,buffer,bytes);
    }
    return 0;
}

void receive_files_from_client(int client_sock,hashtable_t* fileinfos) {
    while(1){
        request_t request;
        read(client_sock,&request,sizeof(request));
        if(request.code == sync_done){
            break;
        }
        if(request.code == buff_start){
            printf("receiving file - %s\n",request.data);
            int fd = open(request.data,O_WRONLY|O_CREAT,0766);
            if(fd == -1){
                perror("fd error");
                send_status_to_client(client_sock,fail);
            }else{
                send_status_to_client(client_sock,succ);
                read_file_from_client(client_sock,fd);
                fileinfo_t file;
                strcpy(file.filename,request.data);
                file.is_updated = 1;
                time_t current_time;
                time(&current_time);
                file.last_mod_time = current_time;
                file.is_dir = 0;
                hashtable_insert(fileinfos,request.data,file);
            }
        }
    }
}


void initial_dir_info(const char* src_dir_path,hashtable_t* fileinfos) {
    DIR* dir = opendir(src_dir_path); //opening source directory
    if (dir == NULL) {
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        //full source path
        char src_path[MAX_PATH_LENGTH];
        snprintf(src_path, MAX_PATH_LENGTH, "%s/%s", src_dir_path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            struct stat file_stat;
            stat(src_path, &file_stat);
            fileinfo_t file;
            strcpy(file.filename,src_path);
            file.last_mod_time = file_stat.st_mtime;
            file.is_dir = 1;
            file.is_updated = 0; 
            hashtable_insert(fileinfos,src_path,file);
            initial_dir_info(src_path,fileinfos);
        } else if (entry->d_type == DT_REG) {
            struct stat file_stat;
            stat(src_path, &file_stat);
            fileinfo_t file;
            strcpy(file.filename,src_path);
            file.last_mod_time = file_stat.st_mtime;
            file.is_dir = 0;
            file.is_updated = 0; 
            hashtable_insert(fileinfos,src_path,file);
        }
    }
    closedir(dir);
}


void* worker_function() {
    while (1) {
        pthread_mutex_lock(&cq_mutex);
        while (conn_client_count == 0) {
            pthread_cond_wait(&cq_full, &cq_mutex);
            if (signal_flag) {
                pthread_mutex_unlock(&cq_mutex);
                pthread_exit(NULL);
            }
        }
        int dequeued_clientsock = dequeue_client(&connected_clients);
        if (dequeued_clientsock != -1) {
            conn_client_count -= 1;
        }
        pthread_cond_signal(&cq_empty);
        pthread_mutex_unlock(&cq_mutex);
        if(dequeued_clientsock!=-1){
            handle_client(dequeued_clientsock);
            close(dequeued_clientsock);
        }
    }
    //pthread_exit(NULL);
    return NULL;
}


void insepect_changes(const char* src_dir_path,hashtable_t* fileinfos,int * dir_file_send_flag){
 DIR* dir = opendir(src_dir_path); //opening source directory
    if (dir == NULL) {
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        //full source path
        char src_path[MAX_PATH_LENGTH];
        snprintf(src_path, MAX_PATH_LENGTH, "%s/%s", src_dir_path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            struct stat file_stat;
            stat(src_path,&file_stat);
            fileinfo_t* file = hashtable_get(fileinfos,src_path);
            if(file == NULL){  //a new directory has been created
                *dir_file_send_flag = 1;
                fileinfo_t file;
                strcpy(file.filename,src_path);
                file.last_mod_time = file_stat.st_mtime;
                file.is_dir = 1;
                file.is_updated = 0;
                hashtable_insert(fileinfos,src_path,file);
            }else if(file!=NULL){ //file in record check if modification has changed
                
                if((file->last_mod_time != file_stat.st_mtime)){ //directory has been modified, not upto date
                    //printf("modified dir- %s\n",file->filename);
                    *dir_file_send_flag  = 1;
                    file->last_mod_time = file_stat.st_mtime;
                    file->is_updated = 0;
                }
            }
            insepect_changes(src_path,fileinfos,dir_file_send_flag);
        } else if (entry->d_type == DT_REG) {
            struct stat file_stat;
            stat(src_path, &file_stat);
            
            fileinfo_t* file = hashtable_get(fileinfos,src_path);
            if(file == NULL){ //new file,not in record, not uptodate but is it because of new file created or received from server
                *dir_file_send_flag = 1;
                fileinfo_t file;
                strcpy(file.filename,src_path);
                file.last_mod_time = file_stat.st_mtime;
                file.is_dir = 0;
                file.is_updated = 0; 
                hashtable_insert(fileinfos,src_path,file);
            }
            else if(file!=NULL){//file in record,let's check modification
                int is_modified = isFileModified(file->last_mod_time,file_stat.st_mtime);
                if(is_modified){ //file modified, not upto date
                    *dir_file_send_flag = 1;
                    file->is_updated = 0;
                    file->last_mod_time = file_stat.st_mtime;                    
                }
            }
        }
    }
    closedir(dir);
}