#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_COMMANDS 20
#define READ_END 0
#define WRITE_END 1

int signal_flag = 0; 
pid_t child_pids[MAX_COMMANDS];
int child_count = 0;


typedef struct {
    char *command;
    char *input_file;
    char *output_file;
} Command;


void trim(char *str);
void create_log_entry(char *command, pid_t pid);
void execute_command(Command *cmd, int input_fd, int output_fd);
void signal_handler(int sig);
void register_signals();







int main() {
    register_signals();
    while (1) {
        
        if (signal_flag) {
            signal_flag = 0;
            continue;
        }

        char input[256];
        printf("term> ");
        fgets(input, sizeof(input), stdin);

        if (strncmp(input, ":q", 2) == 0) {
            break;
        }
        input[strlen(input) - 1] = '\0';

        //parsing commands
        Command commands[MAX_COMMANDS];
        int num_commands = 0;
        char *token = strtok(input, "|");
        while (token) {
            commands[num_commands].command = strdup(token);
            commands[num_commands].input_file = NULL;
            commands[num_commands].output_file = NULL;

            if (strstr(commands[num_commands].command, "<")) {
                char *input_redirect = strchr(commands[num_commands].command, '<');
                *input_redirect = '\0';
                input_redirect++;
                trim(input_redirect); 
                commands[num_commands].input_file = strdup(input_redirect);
            }

            if (strstr(commands[num_commands].command, ">")) {
                char *output_redirect = strchr(commands[num_commands].command, '>');
                *output_redirect = '\0';
                output_redirect++;
                trim(output_redirect); 
                commands[num_commands].output_file = strdup(output_redirect);
            }

                        num_commands++;
            token = strtok(NULL, "|");
        }

        //setting correct input output filedescriptor
        int input_fd = -1;
        int output_fd = -1;


        for (int i = 0; i < num_commands; i++) {
            int pipefd[2];
            if (i < num_commands - 1 && pipe(pipefd) == -1) { //handling pipe 
                perror("pipe");
                exit(EXIT_FAILURE);
            }

            if (commands[i].input_file) {
                input_fd = open(commands[i].input_file, O_RDONLY);
                if (input_fd == -1) {
                    perror("open");
                    exit(EXIT_FAILURE);
                }
            }

            if (commands[i].output_file) {
                output_fd = open(commands[i].output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (output_fd == -1) {
                    perror("open");
                    exit(EXIT_FAILURE);
                }
            } else if (i < num_commands - 1) {
                output_fd = pipefd[WRITE_END];
            }

            //executing command
            execute_command(&commands[i], input_fd, output_fd);


            //closing filedescriptors
            if (input_fd != -1) {
                close(input_fd);
                input_fd = -1;
            }
            if (output_fd != -1) {
                close(output_fd);
                output_fd = -1;
            }

            if (i < num_commands - 1) {
                input_fd = pipefd[READ_END];
                close(pipefd[WRITE_END]);
            }

            free(commands[i].command);
            if (commands[i].input_file) free(commands[i].input_file);
            if (commands[i].output_file) free(commands[i].output_file);
        }
    }

    return 0;
}



void trim(char *str) {
    int len = strlen(str);
    int start = 0;
    int end = len - 1;
    //leading whitespace
    while (isspace(str[start])) {
        start++;
    }
    // ending whitespace
    while (end >= 0 && isspace(str[end])) {
        end--;
    }
    int i;
    for (i = start; i <= end; i++) {
        str[i - start] = str[i];
    }
    str[i - start] = '\0';
}



void create_log_entry(char *command, pid_t pid) {
    char log_filename[50];
    time_t current_time;
    struct tm *time_info;

    time(&current_time);
    time_info = localtime(&current_time);
    strftime(log_filename, sizeof(log_filename), "log_%H%M%S.txt", time_info);

    FILE *log_file = fopen(log_filename, "a");
    if (log_file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    fprintf(log_file, "Command: %s\nPID: %d\n\n", command, pid);
    fclose(log_file);
}

void execute_command(Command *cmd, int input_fd, int output_fd) {
    int status;
    pid_t child_pid = fork();

    if (child_pid == 0) { // Child process
        //uncomment this section to test out signal handling. it will put every command into sleep for 5
        // sleep(10); 
        if (input_fd != -1) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != -1) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }
        if(execl("/bin/sh", "sh", "-c", cmd->command, (char *)NULL) == -1){
            perror("execl");
            exit(EXIT_FAILURE);
        }
    } else if (child_pid > 0) { // Parent process
        child_pids[child_count++] = child_pid;
        if (input_fd != -1) close(input_fd);
        if (output_fd != -1) close(output_fd);
        if (waitpid(child_pid, &status, 0) == -1) {
            perror("waitpid");
            exit(EXIT_FAILURE);
        }else {
            for (int i = 0; i < child_count; i++) {
                if (child_pids[i] == child_pid) {
                    child_pids[i] = child_pids[--child_count];
                    break;
                }
            }
        }
        create_log_entry(cmd->command, child_pid);
    } else { //if fork fails
        perror("fork");
        exit(EXIT_FAILURE);
    }
}

void signal_handler(int sig) {
    switch (sig)
    {
        case SIGINT:
        case SIGTERM:
            printf("\nsignal caught: %d. starting to terminate child processes\n", sig);
            for (int i = 0; i < child_count; i++) {
                printf("killig child: %d\n",child_pids[i]);
                kill(child_pids[i], SIGKILL);
            }
            printf("child processes terminated. use ':q' to exit or enter a command");
            child_count = 0;
            signal_flag =1;
            break;
        default:
            printf("\ncauught signal:  %d. use ':q' to exit\n", sig);
            break;
    }
}



void register_signals(){
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction: SIGINT");
        exit(1);
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction: SIGTERM");
        exit(1);
    }
}

