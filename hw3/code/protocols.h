#ifndef PROTOCOLS_H
#define PROTOCOLS_H


#include <string.h>

typedef enum response_protocol{
    connEstablished=1,
    connWaiting=2,
    connDeclined=3,
    resBuffer=4,
    resFail=5,
    resComplete=6,
}resprotocol_t;


typedef enum reqprotocol{
    invalid=-1,
    Connect=1,
    tryConnect=2,
    help=3, 
    list=4, 
    readF=5, 
    writeT=6, 
    upload=7, 
    download=8, 
    quit=9, 
    killServer=10,
    
} reqprotocol_t;



reqprotocol_t get_protocol(char* command){
    if (strcmp(command, "help") == 0) {
        return help;
    }else if(strcmp(command, "Connect") == 0){
        return Connect;
    }else if(strcmp(command, "tryConnect") == 0){
        return tryConnect;
    }else if (strcmp(command, "list") == 0) {
        return list;
    } else if (strncmp(command, "readF", 5) == 0) {
        return readF;
    } else if (strncmp(command, "writeT", 6) == 0) {
        return writeT;
    } else if (strncmp(command, "upload", 6) == 0) {
        return upload;
    } else if (strncmp(command, "download", 8) == 0) {
        return download;
    } else if (strcmp(command, "quit") == 0) {
        return quit;
    } else if (strcmp(command, "killServer") == 0) {
        return killServer;
    } else {
        return invalid;
    }
}



#endif