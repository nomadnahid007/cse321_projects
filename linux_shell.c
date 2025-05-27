#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdbool.h>
#include <signal.h>
#include <ctype.h>

#define MAX_VAL 275
#define MAX_ARGS 50
#define HISTORY_SIZE 100


void handle_sigint(int sig);
void show_history(void);
void parse_input(char *inp, char *args[], int *input_redir, char **input_file,  int *output_redir, char **output_file);
void execute_with_redirection(char *args[], int input_redir, char *input_file,int output_redir, char *output_file);
int builtin_command(char *args[]);
int handle_redirections(char *args[]);
void external_command(char *args[]);
void compact_args(char *args[]);
void remove_space(char *str);
int piping(char *inp);
void take_input(char *inp);
void add_to_history(const char *command);
void show_history(void);


// Signal handler for SIGINT (Ctrl+C)
void handle_sigint(int sig) {
    write(STDOUT_FILENO, "\n^C (command cancelled)\n", 25);
    
    
    write(STDOUT_FILENO,"siu> ", 5); //Abar prompt dekhabe shell e 
    
    // Just output ta quick display korar jonno
    fflush(stdout);
}

// Function to show history of commands
void show_history(void);

// Function to parse the input into arguments
void parse_input(char *inp, char *args[], int *input_redir, char **input_file, 
                int *output_redir, char **output_file) {
    *input_redir = 0;
    *output_redir = 0;
    *input_file = NULL;
    *output_file = NULL;

    char *token = strtok(inp, " ");
    int i = 0;
    
    while (token != NULL) {
        if (strcmp(token, "<") == 0) {
            *input_redir = 1;
            token = strtok(NULL, " ");
            if (token) *input_file = token;
        } 
        else if (strcmp(token, ">") == 0) {
            *output_redir = 1; 
            token = strtok(NULL, " ");
            if (token) *output_file = token;
        }
        else if (strcmp(token, ">>") == 0) {
            *output_redir = 2; // 2 for append
            token = strtok(NULL, " ");
            if (token) *output_file = token;
        }
        else {
            args[i++] = token;
        }
        token = strtok(NULL, " ");
    }
    args[i] = NULL;
}

void execute_with_redirection(char *args[], int input_redir, char *input_file,
                            int output_redir, char *output_file) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Handle input redirection
        if (input_redir && input_file) {
            int fd = open(input_file, O_RDONLY); // <
            if (fd < 0) {
                perror("open input file");
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        // Handle output redirection
        if (output_redir && output_file) {
            int fd;
            if (output_redir == 2) {
                fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644); // >>
            }
            else {
                fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644); // >
                         
            }
            
            if (fd < 0) {
                perror("Output file error");
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        
        // Execute the command
        execvp(args[0], args);
        perror("execvp failed");
        exit(1);
    } 
    else if (pid > 0) {
        wait(NULL);
    } 
    else {
        perror("fork failed");
    }
}

// Handling built-in commands
int builtin_command(char *args[]) {
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            printf("cd: expected argument\n");
        } else if (chdir(args[1]) != 0) {
            perror("cd");
        }
        return 1;
    } else if (strcmp(args[0], "clear") == 0) {
        printf("\033[H\033[J");
        return 1;
    } else if (strcmp(args[0], "exit") == 0) {
        exit(0); // Exits the shell
    } else if (strcmp(args[0], "history") == 0) {
        show_history();
        return 1;
    }
    return 0;
}

// Redirection code
int handle_redirections(char *args[]) {
    int fd_in = -1, fd_out = -1;
    int append = 0;

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0 && args[i+1] != NULL) {
            if ((fd_in = open(args[i+1], O_RDONLY)) < 0) {
                perror("input redirection failed");
                return -1;
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
            args[i] = args[i+1] = NULL;
        }
        else if (strcmp(args[i], ">") == 0 && args[i+1] != NULL) {
            if ((fd_out = open(args[i+1], O_WRONLY|O_CREAT|O_TRUNC, 0644)) < 0) {
                perror("output redirection failed");
                return -1;
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
            args[i] = args[i+1] = NULL;
        }
        else if (strcmp(args[i], ">>") == 0 && args[i+1] != NULL) {
            if ((fd_out = open(args[i+1], O_WRONLY|O_CREAT|O_APPEND, 0644)) < 0) {
                perror("append redirection failed");
                return -1;
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
            args[i] = args[i+1] = NULL;
        }
    }
    return 0;
}


// External command execution
void external_command(char *args[]) {
    pid_t pid = fork();
    
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        execvp(args[0], args);
        perror("execvp failed");
        exit(1);
    } 
    else if (pid > 0) {
        wait(NULL);
    } 
    else {
        perror("fork failed");
    }
}


// Removing leading and trailing spaces in commands
void remove_space(char *str) {
    int st = 0;
    while (str[st] == ' ') {
        st++;
    }
    if (st > 0) {
        int i = 0;
        while (str[st]) str[i++] = str[st++];
        str[i] = '\0';
    }
    int end = strlen(str) - 1;
    while (end >= 0 && str[end] == ' ') str[end--] = '\0';
}


// Piping code 
int piping(char *inp) {
    pid_t pid;
    int pipefd[2];
    int counter = 0;
    char *commands[MAX_ARGS];
    int prev_pipe_read = -1;
    int status = 0;

    // Split commands by pipe
    commands[counter] = strtok(inp, "|");
    while (commands[counter] != NULL && counter < MAX_ARGS - 1) {
        counter++;
        commands[counter] = strtok(NULL, "|");
    }

    for (int i = 0; i < counter; i++) {
        // Create pipe for all commands except last
        if (i < counter - 1) {
            if (pipe(pipefd) == -1) {
                perror("pipe failed");
                return -1;
            }
        }

        pid = fork();
        if (pid == 0) { // Child process
            // Handle input from previous command
            if (prev_pipe_read != -1) {
                dup2(prev_pipe_read, STDIN_FILENO);
                close(prev_pipe_read);
            }

            // Handle output to next command
            if (i < counter - 1) {
                close(pipefd[0]); // Close read end
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }

            // Process current command with redirection support
            remove_space(commands[i]);
            
            char *args[MAX_ARGS];
            int input_redir = 0;
            int output_redir = 0;
            char *input_file = NULL;
            char *output_file = NULL;
            
            parse_input(commands[i], args, &input_redir, &input_file, 
                       &output_redir, &output_file);

            // Handle input redirection (overrides pipe input)
            if (input_redir) {
                int fd = open(input_file, O_RDONLY);
                if (fd < 0) {
                    perror("open input file");
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            // Handle output redirection (overrides pipe output)
            if (output_redir) {
                int flags = O_WRONLY | O_CREAT;
                if (output_redir == 1) flags |= O_TRUNC;  // >
                else flags |= O_APPEND;                   // >>
                
                int fd = open(output_file, flags, 0644);
                if (fd < 0) {
                    perror("open output file");
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            // Execute command
            signal(SIGINT, SIG_DFL);
            execvp(args[0], args);
            perror("execvp failed");
            exit(1);
        } 
        else if (pid < 0) {
            perror("fork failed");
            return -1;
        }

        // Parent process cleanup
        if (prev_pipe_read != -1) close(prev_pipe_read);
        if (i < counter - 1) {
            close(pipefd[1]); // Close write end
            prev_pipe_read = pipefd[0]; // Save read end for next command
        }

        // Wait for current command to finish
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            if (prev_pipe_read != -1) close(prev_pipe_read);
            return -1;
        }
    }

    // Final cleanup
    if (prev_pipe_read != -1) close(prev_pipe_read);
    return 0;
}


// Function to handle inputs, parsing and executing commands
void take_input(char *inp) {
    char *semi_comm;
    char *saveptr1;

    semi_comm = strtok_r(inp, ";", &saveptr1);

    while (semi_comm != NULL) {
        remove_space(semi_comm);

        bool go_next = true;
        char *and_comm;
        char *saveptr2;

        and_comm = strtok_r(semi_comm, "&&", &saveptr2);

        while (and_comm != NULL) {
            remove_space(and_comm);

            if (go_next) {
                if (strchr(and_comm, '|')) {
                    int status = piping(and_comm);
                    go_next = (status == 0);
                } else {
                    char *args[MAX_ARGS];
                    int input_redir = 0;
                    int output_redir = 0;
                    char *input_file = NULL;
                    char *output_file = NULL;
                    
                    parse_input(and_comm, args, &input_redir, &input_file, 
                              &output_redir, &output_file);

                    if (builtin_command(args)) {
                        go_next = true;
                    } else {
                        pid_t pid = fork();
                        if (pid == 0) {

                            // Handle redirection in child
                            if (input_redir && input_file) {
                                int fd = open(input_file, O_RDONLY);
                                if (fd < 0) {
                                    perror("open input file");
                                    exit(1);
                                }
                                dup2(fd, STDIN_FILENO);
                                close(fd);
                            }
                            
                            if (output_redir && output_file) {
                                int fd;
                                if (output_redir == 2) {
                                    fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, 0644); // >>
                                } else {
                                    fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644); // >
                                }
                                
                                
                                if (fd < 0) {
                                    perror("open output file");
                                    exit(1);
                                }
                                dup2(fd, STDOUT_FILENO);
                                close(fd);
                            }
                            
                            signal(SIGINT, SIG_DFL);
                            execvp(args[0], args);
                            perror("Exec failed");
                            exit(1);
                        } else {
                            int status;
                            wait(&status);
                            go_next = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
                        }
                    }
                }
            }

            and_comm = strtok_r(NULL, "&&", &saveptr2);
        }

        semi_comm = strtok_r(NULL, ";", &saveptr1);
    }
}

// To handle history of shell command
char *history[HISTORY_SIZE];
int history_count = 0;

void add_to_history(const char *command) {

    if (strlen(command) == 0 || command[0] == '\n') { // Ignoring empty commands
        return; 
    }
    if (history_count == HISTORY_SIZE) {
        free(history[0]);
        for (int i = 1; i < HISTORY_SIZE; i++) {
            history[i - 1] = history[i];
        }
        history_count--;
    }
    history[history_count++] = strdup(command);
}


// Show history
void show_history(void) {
    for (int i = 0; i < history_count; i++) {
        printf("%d: %s\n", i + 1, history[i]);
    }
}


// Main function to handle user input and execute commands
int main() {
    char buffer[MAX_VAL];
    signal(SIGINT, handle_sigint);

    while (true) {
        printf("siu> ");  
        if (fgets(buffer, MAX_VAL, stdin) == NULL) {
            break;
        }

        buffer[strcspn(buffer, "\n")] = '\0';  // Remove newline

        // Checking for history command
        if (buffer[0] == '!' && isdigit(buffer[1])) {
            int idx = atoi(buffer + 1) - 1;
            if (idx >= 0 && idx < history_count) {
                strcpy(buffer, history[idx]);
                printf("Executing command from history: %s\n", buffer);
            } else {
                printf("Invalid history index\n");
                continue;
            }
        }
        // Checking for empty input
        if (strlen(buffer) == 0 || buffer[0] == '\n') {
            continue;
        }

        // Adding command to history
        if (strlen(buffer) > 0) {
            add_to_history(buffer);
        }

        if (strcmp(buffer, "exit") == 0) {
            break;
        }

        take_input(buffer);
    }

    // Free history memory
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
    }

    return 0;
}




