#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>


#define BUF_SIZE 1024

/*
this will duplicate the file descriptor and return a file
descriptor which is greater than or equal to 0 if the fd is
not in use
*/
int dup(int oldfd) {
    int fd = fcntl(oldfd, F_DUPFD, 0); 
    return fd;
}

int dup2(int oldfd, int newfd) {

    //if oldfd is same as newfd thaen checking if oldfd exists
    if (oldfd == newfd) {
        if (fcntl(oldfd, F_GETFL) == -1) {
            errno = EBADF;
            return -1;
        }
        return oldfd;
    }
    //closing newfd if it's alredy open
    if(close(newfd) == -1 && errno != EBADF)
        return -1;

    fcntl(oldfd, F_DUPFD, newfd);
    return newfd;
}

void verify_dup(){
    // Open a file for reading and writing
    int fd1 = open("file.txt", O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd1 == -1) {
        perror("open");
        return;
    }

    // Write some data to the file
    if (write(fd1, "hello", 5) != 5) {
        perror("write");
        return;
    }

    // Duplicate the file descriptor
    int fd2 = dup(fd1);
    if (fd2 == -1) {
        perror("dup");
        return;
    }
    // verifying if they have same offset value
    off_t offset1 = lseek(fd1, 0, SEEK_CUR);
    off_t offset2 = lseek(fd2, 0, SEEK_CUR);
    if (offset1 != offset2) {
        printf("file offsets different(dup): %lld != %lld\n", (long long)offset1, (long long)offset2);
        return;
    }else{
        printf("same file offsets (dup): %lld == %lld\n", (long long)offset1, (long long)offset2);
        return;
    }
    // Close the file descriptors
    close(fd1);
    close(fd2);
    return;
}


void verify_dup2(){
    // Open a file for reading adn writing
    int fd1 = open("file.txt", O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd1 == -1) {
        perror("open");
        return;
    }

    //setting the offset of fd1 at 2
    off_t offset1 = lseek(fd1,2,SEEK_SET);

    //creating a fd copy using dup2
    int fd2 = dup2(fd1,5);
    //getting the offset of second file descriptor
    off_t offset2 = lseek(fd2, 0, SEEK_CUR);
    
    //checking if the offsets are same or not
    if (offset1 != offset2) {
        printf("file offsets different(dup2): %lld != %lld\n", (long long)offset1, (long long)offset2);
        return;
    }else{
        printf("same file offsets (dup2): %lld == %lld\n", (long long)offset1, (long long)offset2);
        return;
    }
}



int main() {
    //making a copy of stdout using custom dup
    int fd = dup(1);
    if(fd == -1){
        perror("dup");
        return 0;
    }
    char buffer1[] = "writen with copied stdout (dup) which points to stdout\n";
    int byteswritten = 0;
    while((byteswritten = write(fd,buffer1,strlen(buffer1))) == -1 && errno == EINTR); //while there is interruption by signal keep writing
    if (byteswritten == -1){ //if fails for anything other than EINTR
        perror("write");
        return 0;
    }
    //making a copy of fd using custom dup2 which point to stdout
    int fd1 = dup2(fd,4);
    if(fd1 == -1){
        perror("dup2");
        return 0;
    }
    char buffer2[] = "writen with copied fd (dup2) which points to stdout\n";
    while((byteswritten = write(fd1,buffer2,strlen(buffer2))) == -1 && errno == EINTR); //while there is interruption by signal keep writing
    if (byteswritten == -1){ //if fails for anything other than EINTR
        perror("write");
        return 0;
    }

    //verifying dup and dup2
    verify_dup();
    verify_dup2();
    return 0;
}
