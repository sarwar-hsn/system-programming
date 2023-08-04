#include "common.h"
#include "hashtable.h"
#include "filequeue.h"

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

volatile sig_atomic_t quit = 0;

//globals
int client_sock;
char client_dir_path[CLIENT_TEMPLATE_LEN];
char log_dir_path[LOG_DIR_TEMPLATE_LEN];
char logfilepath[LOG_DIR_TEMPLATE_LEN + 20];
filequeue_t filesendqueue;
hashtable_t* fileinfos;
int dir_file_send_flag = 0;

//function definitions
int send_to_server(protocol_t, char*);
void initial_dir_file_info(const char* src_dir_path);
int send_status_to_server(protocol_t code);
void receive_dir_from_server();
void receive_files_from_server();
void send_dir_to_server(const char* src_dir_path);
void send_files_to_server();
int read_file_to_server(int fromfd);
int read_file_from_server(int fromfd,int tofd);
void insepect_changes(const char* src_dir_path);
void write_log(const char *logfilename, const char *operation, const char *message);
void cleanup();


void sigint_handler(int sig_num) {
    printf("\n\nTermination signal received.Please wait,shutting down...\n\n");
    quit = 1;
}



int main(int argc, char* argv[]){
    struct sockaddr_in server_addr;
    int is_dir_created;
    char* client_dir_name;
    int port;
    char* ip_address;
    protocol_t response_code;
    // pthread_t monitor_thread;

    if(argc < 3) {
        printf("Usage: %s <client_dir_name> <port> [optional - ip_address (default - 127.0.0.1)]\n", argv[0]);
        return 1;
    }

    client_dir_name = argv[1];
    printf("client sync direcotory - %s\n\n",client_dir_name);
    port = atoi(argv[2]);

    if(argc >= 4) {
        ip_address = argv[3];
    } else {
        ip_address = "127.0.0.1"; // Default IP
    }


    struct sigaction sa;
    sa.sa_handler = &sigint_handler;
    sa.sa_flags = SA_RESTART; 
    sigfillset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error cannot handle SIGINT");
    }

    init_filequeue(&filesendqueue);
    fileinfos = hashtable_create(MAX_TABLE_SIZE);
    if(!fileinfos){
        fprintf(stderr,"failed to create hashtable\n");
        exit(EXIT_FAILURE);
    }

    
    snprintf(client_dir_path,CLIENT_TEMPLATE_LEN,CLIENT_DIR_TEMPLATE,client_dir_name);
    is_dir_created = mkdir(client_dir_path,0766);
    if((is_dir_created == -1) && (errno!=EEXIST)){
        perror("failed to locate clientdir");
        exit(EXIT_FAILURE);
    }

   

    is_dir_created = mkdir(LOG_DIR,0766);
    if((is_dir_created == -1) && (errno != EEXIST)){
        perror("Failed to create LOG directory");
        exit(EXIT_FAILURE);
    }

    snprintf(log_dir_path, LOG_DIR_TEMPLATE_LEN, CLIENT_LOG_DIR_TEMPLATE,client_dir_name);
    is_dir_created = mkdir(log_dir_path,0766);
    if((is_dir_created == -1) && (errno != EEXIST)){
        perror("Failed to create client specific LOG directory");
        exit(EXIT_FAILURE);
    }

    
    snprintf(logfilepath, sizeof(logfilepath), LOG_FILE_TEMPLATE, log_dir_path);

    //creating socket
    client_sock = socket(AF_INET,SOCK_STREAM,0);
    if(client_sock == -1){
        perror("client socket");
        return EXIT_FAILURE;
    }
    //connecting to server
    server_addr.sin_family=AF_INET;
    
    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
        perror("invalid server address");
        return EXIT_FAILURE;
    }
    server_addr.sin_port = htons(port);

    if(connect(client_sock,(struct sockaddr*)&server_addr,sizeof(server_addr)) == -1){
        perror("failed to connect server");
        return EXIT_FAILURE;
    }


    printf("Waiting for server to accept request....\n");
    //response from server
    recv(client_sock,&response_code,sizeof(int),0);
    if(response_code == connected){
        printf("connection established with server...\n\n");
    }

    //server-to-client sync
    send_to_server(normal,client_dir_path); //sending root dir to server
    recv(client_sock,&response_code,sizeof(int),0); //server check if sync need

    initial_dir_file_info(client_dir_path);

    if(response_code == sync_done){
        printf("empty server dir sync done\n");
    }else if(response_code == sync_start){
        printf("server will be sending files\n");
        receive_dir_from_server();
        receive_files_from_server();
    }
    //server-to-client sync done

    //client-to-server sync start
    send_dir_to_server(client_dir_path);
    send_to_server(sync_done,"done");//syncing dir done
    send_files_to_server();


    //setting timeout
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    if (setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        perror("setsockopt failed");
        return 1;
    }
    
    while(1){
        printf("waiting for response\n");
        if(quit){
            cleanup();
            exit(EXIT_SUCCESS);
        }
        protocol_t res_code;
        ssize_t bytes_read = read(client_sock,&res_code,sizeof(int));
        if (bytes_read <= 0) {
            if (bytes_read == 0) {
                printf("Client closed the connection\n");
                break;
            } 
        }else if(bytes_read > 0){
            if(res_code == sync_start){
                printf("receiving file/dir from server\n");
                if(dir_file_send_flag!=1){
                    send_status_to_server(sync_start);
                }else{
                    send_status_to_server(sync_wait);
                }
                //another response from server
                 if(response_code == sync_done){
                    printf("empty server dir sync done\n");
                }else if(response_code == sync_start){
                    printf("server will be sending files\n");
                    receive_dir_from_server();
                    receive_files_from_server();
                }

            }
        }
        if(dir_file_send_flag != 1){
            insepect_changes(client_dir_path);
        }
        //hashtable_print_updated(fileinfos);
    
        if(dir_file_send_flag == 1){
            send_status_to_server(sync_start);
            send_dir_to_server(client_dir_path);
            send_to_server(sync_done,"dir sync done");//syncing dir done
            send_files_to_server();
            dir_file_send_flag = 0;
        }
    }
    
    close(client_sock); // Remember to close the socket when you're done with it
    return 0;
}

void cleanup(){
    hashtable_free(fileinfos);
}

int send_status_to_server(protocol_t code){
    int codetosend = code;
    int bytes_sent = send(client_sock, &codetosend, sizeof(int), 0);
    return bytes_sent;
}

int send_to_server(protocol_t code,char* message){
    request_t request;
    request.code = code;
    strcpy(request.data, message);
    request.data[strlen(request.data)]='\0';
    int bytes_written = write(client_sock,&request,sizeof(request));
    return bytes_written;
}

//initially all files are markes as not upto date and added to record
void initial_dir_file_info(const char* src_dir_path) {
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
            initial_dir_file_info(src_path);
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

int read_file_to_server(int fromfd){
    int buffer_length = 1024;
    char buffer[buffer_length];
    int bytes;
    //int byteswritten = 0;
    while ((bytes = read(fromfd, buffer, buffer_length - 1)) > 0) {
        buffer[bytes] = '\0';
        if (write(client_sock, buffer, bytes) != bytes) {
            break;
        } 
        //receiveing acknowledgement before next read
        int amount_received;
        recv(client_sock,&amount_received,sizeof(int),0);
        if(quit){
            break;
        }
    }
    write(client_sock,"EOF",3);
    return 0;
}

void send_files_to_server() {  
    while((!is_filequeue_empty(&filesendqueue)) && (quit==0)) {
        task_t task = dequeue_task(&filesendqueue);
        if(task.client_sock !=-1){
            int src_fd = open(task.filename,O_RDONLY);
            if(src_fd != -1){
                //creating file in server
                request_t filefd_req;
                strcpy(filefd_req.data,task.filename);
                filefd_req.code = buff_start;
                write(client_sock,&filefd_req,sizeof(filefd_req));

                //now wait for response from server
                protocol_t serverfd_status_res;
                read(client_sock,&serverfd_status_res,sizeof(int));
                if(serverfd_status_res == succ){
                    printf("sending to server %s\n",task.filename);
                    write_log(logfilepath,"sending file",task.filename);
                    int total = read_file_to_server(src_fd);
                }
            }else{
                close(src_fd);
            }
        }
    }
    request_t syncdonereq;
    strcpy(syncdonereq.data, "all files synced");
    syncdonereq.code = sync_done;
    write(client_sock,&syncdonereq,sizeof(syncdonereq));

}

//just sends files and folders that are marked as not updated
void send_dir_to_server(const char* src_dir_path){
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
            if(file!=NULL && file->is_updated == 0){//is not synced with server. need to send
                file->is_updated = 1;
                protocol_t response_code;
                char path[MAX_PATH_LENGTH];
                strcpy(path,src_path);
                send_to_server(dir_type,path);
                recv(client_sock,&response_code,sizeof(response_code),0);
                if(response_code == fail){
                    write_log(logfilepath,"dir creation failed in server",src_path);
                    printf("directory %s creation failed in server\n",entry->d_name);
                }
            }
            write_log(logfilepath,"sending dir",src_path);
            send_dir_to_server(src_path);//recursive call
        } else if(entry->d_type == DT_REG) {
            fileinfo_t* file = hashtable_get(fileinfos,src_path);
            if(file!=NULL){
                if(file->is_updated == 0){ //file marked to be sent
                    task_t task; 
                    strcpy(task.filename,src_path);
                    task.client_sock = client_sock;
                    enqueue_task(&filesendqueue,task);
                    file->is_updated = 1;
                }
            }
        }
    }
    closedir(dir);
}

//now all files are marked as updated 
void insepect_changes(const char* src_dir_path){
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
                dir_file_send_flag = 1;
                fileinfo_t file;
                strcpy(file.filename,src_path);
                file.last_mod_time = file_stat.st_mtime;
                file.is_dir = 1;
                file.is_updated = 0;
                hashtable_insert(fileinfos,src_path,file);
            }else if(file!=NULL){ //file in record check if modification has changed
                
                if((file->last_mod_time != file_stat.st_mtime)){ //directory has been modified, not upto date
                    //printf("modified dir- %s\n",file->filename);
                    dir_file_send_flag  = 1;
                    file->last_mod_time = file_stat.st_mtime;
                    file->is_updated = 0;
                }
            }
            insepect_changes(src_path);
        } else if (entry->d_type == DT_REG) {
            struct stat file_stat;
            stat(src_path, &file_stat);
            
            fileinfo_t* file = hashtable_get(fileinfos,src_path);
            if(file == NULL){ //new file,not in record, not uptodate but is it because of new file created or received from server
                dir_file_send_flag = 1;
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
                    dir_file_send_flag = 1;
                    file->is_updated = 0;
                    file->last_mod_time = file_stat.st_mtime;                    
                }
            }
        }
    }
    closedir(dir);
}

//receiving directory, add to record and mark upto date with modification time
void receive_dir_from_server(){
    response_t server_response;
    while (1){
        recv(client_sock,&server_response,sizeof(server_response),0);
        if(server_response.code == dir_type){
            char path[MAX_PATH_LENGTH];
            server_response.data[strlen(server_response.data)]='\0';
            strcpy(path,server_response.data);

            int is_dir_created = mkdir(path,0766);  
            if(is_dir_created!=-1){ //created new directory sent by server
                time_t current_time;
                time(&current_time);
                fileinfo_t file;
                strcpy(file.filename,path);
                file.is_updated = 1;
                file.last_mod_time = current_time;
                file.is_dir = 1;
                hashtable_insert(fileinfos,path,file);
                write_log(logfilepath,"dir received",path);
                send_status_to_server(succ);
            }else if(is_dir_created == -1){ //directory creating failed
                if(errno == EEXIST){ //directory existed
                    fileinfo_t* file = hashtable_get(fileinfos,path);
                    if(file!=NULL){
                        // time_t current_time;
                        // time(&current_time);
                        file->is_updated = 1;
                        file->is_dir = 1;
                        //file->last_mod_time = current_time;
                    }
                send_status_to_server(succ);
                }else{ //neither created nor found
                    send_status_to_server(fail);
                }
            }
        }else if((server_response.code == sync_done )){
            printf("server-to-client directory sync done\n");
            break;
        }
    }
}


//receiving directory, add to record and mark upto date with modification time
void receive_files_from_server() {
    while(!quit){
        response_t response;
        read(client_sock,&response,sizeof(response));
        printf("client receiving - %s\n",response.data);
        write_log(logfilepath,"file received",response.data);
        if(response.code == sync_done){
            break;
        }
        if(response.code == buff_start){
            int fd = open(response.data,O_WRONLY|O_CREAT,0766); //always creating file no matter what is in client            
            if(fd == -1){
                perror("fd error");
                send_status_to_server(fail);
            }else{
                send_status_to_server(succ);
                read_file_from_server(client_sock,fd);
                fileinfo_t file;
                strcpy(file.filename,response.data);
                file.is_updated = 1;
                time_t current_time;
                time(&current_time);
                file.last_mod_time = current_time;
                file.is_dir = 0;
                hashtable_insert(fileinfos,response.data,file);
            }
        }
    }
}

//reading from socket and write to file
int read_file_from_server(int fromfd,int tofd){
    int buffer_length = 1024;
    char buffer[buffer_length];
    int bytes;
    ssize_t byteswritten = 0;
    while ((bytes = read(fromfd, buffer, buffer_length-1)) > 0) {
        buffer[bytes]='\0';
        if(strcmp(buffer,"EOF")==0){
            break;
        }
        send_status_to_server(bytes); //sending acknowledgement
        write(tofd,buffer,bytes);
    }
    return 0;
}



void write_log(const char *logfilename, const char *operation, const char *message) {
    FILE *file = fopen(logfilename, "a");
    if (file == NULL) {
        fprintf(stderr, "Failed to open log file %s\n", logfilename);
        return;
    }
    time_t now;
    time(&now);
    struct tm *time_info = localtime(&now);
    char timestamp[20]; // Buffer large enough for "YYYY-MM-DD HH:MM:SS\0"
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);
    fprintf(file, "[%s] - [%s] - %s\n", timestamp, operation, message);
    fclose(file);
}




