#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <fcntl.h>

void usage(){
	fprintf(stderr,"usage: appendMeMore filename num-bytes [x]\n");
}

int main(int argc, char *argv[]){
	char* filename;
	int num_bytes;
	int isX = 0;
	/*if number of arguments is not satisfied or invalid argument, we print usage in standard error*/
	if (argc < 3 || argc > 4){
		usage();
		return 0;
	}else{
		filename = argv[1];
		num_bytes = atoi(argv[2]); 
		if (num_bytes == 0 && strcmp(argv[2],"0")!=-0){ //if the second is not numeric value
			usage();
			return 0;
		}
		if(argc == 4){
			if(strcmp(argv[3], "x") == 0 || strcmp(argv[3], "X") == 0){
				isX = 1;
			}else{
				usage();
				return 0;
			}
		}
	}

	fprintf(stdout,"filename: %s, num-bytes: %d, [x]: %d\n",filename,num_bytes,isX); //printing arguments value
	
	int flags;
	if(isX){
		flags = O_WRONLY | O_APPEND | O_CREAT | O_EXCL;
	}else{
		flags = O_WRONLY | O_CREAT | O_EXCL;
	} 
	int mode = 0666;

	
	int fd = open(filename,flags,mode); //trying to open file descriptor with a filename
	if ((fd == -1) && (errno == EEXIST)){ // if the file already exists then opening file descriptor without O_CREAT
		if (isX){
			fd = open(filename,O_WRONLY|O_APPEND,mode);
		}else{
			fd = open(filename,O_WRONLY,mode);
		}
	}
	
	//checking if the error is not because of EEXIST. If so printing to standard error and exit
	if(fd < 0){
		perror("open");
		return 0;
	}

	int byteswritten = 0;
	int totalbyte = 0;
	for(int i = 1; i <= num_bytes; i++){
		char c = 'a';
		if (isX){ // is [x] argument is provided then we constantly calling lseek
			if(lseek(fd,0,SEEK_END) < 0){
				perror("lseek");
				return 0;
			}
		}
		while((byteswritten = write(fd,&c,1)) == -1 && errno == EINTR); //while there is interruption by signal keep writing
		if (byteswritten == -1){ //if fails for anything other than EINTR
			perror("write");
			return 0;
		}
		totalbyte+=byteswritten;
	}
	if(totalbyte == num_bytes){
		fprintf(stdout,"done writing\n");
	}
	if (close(fd) < 0) {
		perror("close");
		return 0;
	}
	return 0;
}
