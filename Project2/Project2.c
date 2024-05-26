#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>

// Defining the maximum line and arguments
#define MAX_LINE 1024
#define MAX_ARGS 64

//global variable to kepe track of child processes
pid_t child_pid = -1;
void exit_shell(int sig) {
    printf("\nExiting shell...\n");
    exit(0);
}

void end_execution(int sig) {
    if (child_pid > 0) {
        kill(child_pid, SIGKILL); // Kill the child process
        printf("\nCommand interrupted. Returning to prompt...\n");
    }
}

void split_commands(char *input, char **commands) {
    //splits the input into commands using the delimiter ";"
    char *token = strtok(input, ";");
    int i = 0;
    //stores the commands in the commands array
    while (token != NULL && i < MAX_ARGS - 1) {
        commands[i++] = token;
        token = strtok(NULL, ";");
    }
    commands[i] = NULL;
}

void parse_command(char *cmd, char **args) {
    //splits the command into arguments using the delimiter " "
    char *token = strtok(cmd, " \n");
    int i = 0;
    //stores the arguments in the args array
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \n");
    }
    args[i] = NULL;
}

void execute_command(char **args) {
    //makes a new process
    child_pid = fork();
    if (child_pid < 0) {
        perror("Fork failed");
        exit(1);
    } else if (child_pid == 0) {
        // Child process
        if (execvp(args[0], args) == -1) {
            perror("Exec failed");
            exit(1);
        }
    } else {
        // Parent process
        int status;
        //waits for the child process to finish
        waitpid(child_pid, &status, 0);
        child_pid = -1; // Reset child_pid after command execution
    }
}
int main(int argc, char *argv[]) {
    //setting up signal handlers for SIGINT and SIGQUIT
    //SIGINT is used to exit the shell
    //SIGQUIT is used to end the execution of the current command
    signal(SIGINT, exit_shell);
    signal(SIGQUIT, end_execution);

    char line[MAX_LINE];
    char *args[MAX_ARGS];
    char *commands[MAX_ARGS];

//argc would be 2 because there are two arguments: 
//the program name (./myshell) and the batch file name (batchfile.txt)
    if (argc == 2) {
        //opens argv[1](the file) in read mode
        FILE *batch_file = fopen(argv[1], "r");
        if (!batch_file) {
            perror("Failed to open batch file");
            return 1;
        }
        //fgets reads the line from the file while 
        while (fgets(line, sizeof(line), batch_file)) {
            printf("Batch command: %s", line);
            parse_command(line, args);
            //executes the command
            execute_command(args);
        }
        fclose(batch_file);
    } else {
        // Interactive mode( ifno batch file is provided)
        while (1) {
            printf("GLShell> ");
            if (!fgets(line, sizeof(line), stdin)) {
                break; // Handle EOF
            }
            split_commands(line, commands);
            for (int i = 0; commands[i] != NULL; i++) {
                parse_command(commands[i], args);
                execute_command(args);
            }
        }
    }

    return 0;
}
