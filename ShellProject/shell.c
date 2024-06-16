#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>

// ANSI color codes
#define RESET "\033[0m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define BLUE "\033[34m"
#define PURPLE "\033[35m"
#define CYAN "\033[36m"

// Defining the maximum line and arguments
#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_HISTORY 50

// Virtual memory definitions
#define PHYSICAL_MEMORY_SIZE (16 * 1024 * 1024) // 16 MB
#define VIRTUAL_MEMORY_SIZE (64 * 1024 * 1024)  // 64 MB
#define KERNEL_MEMORY_SIZE (4 * 1024 * 1024)    // 4 MB
#define PAGE_SIZE 4096                          // 4 KB
#define MAX_PROCESSES 64

// Structures for page tables and frame tables
typedef struct {
    int frame_number;
    int valid;
} PageTableEntry;

typedef struct {
    int process_id;
    int page_number;
    int valid;
} FrameTableEntry;

typedef struct {
    int process_id;
    int page_number;
    int access_time;
} PageAccessEntry;

typedef enum { NEW, READY, RUN, WAIT, TERMINATED } State;

typedef struct Process {
    int pid;
    int burst_time;
    int remaining_time;
    int io_time;
    int priority;
    State state;
    char command[256];
    struct Process* next;
} Process;

typedef struct {
    Process* head;
    Process* tail;
} Queue;

// Global memory management structures
PageTableEntry page_table[MAX_PROCESSES][VIRTUAL_MEMORY_SIZE / PAGE_SIZE];
PageTableEntry kernel_page_table[KERNEL_MEMORY_SIZE / PAGE_SIZE];
FrameTableEntry frame_table[PHYSICAL_MEMORY_SIZE / PAGE_SIZE];
PageAccessEntry page_access_history[PHYSICAL_MEMORY_SIZE / PAGE_SIZE];

Queue readyQueue;
Process* processes[MAX_PROCESSES];
int process_count = 0;
int free_frame_index = 0;
int current_time = 0;

// Global variable to keep track of child processes
pid_t child_pid = -1;

// Function to log a command to History.txt
void log_command(const char *command) {
    FILE *history_file = fopen("History.txt", "a");
    if (history_file == NULL) {
        perror(RED "Failed to open history file" RESET);
        return;
    }
    fprintf(history_file, "%s\n", command);
    fclose(history_file);
}

void exit_shell(int sig) {
    printf(RED "\nExiting shell...\n" RESET);
    exit(0);
}

void end_execution(int sig) {
    if (child_pid > 0) {
        kill(child_pid, SIGKILL); // Kill the child process
        printf(YELLOW "\nCommand interrupted. Returning to prompt...\n" RESET);
    }
}

// Function to split the input into commands if you want to run multiple commands at once
void split_commands(char *input, char **commands) {
    // Splits the input into commands using the delimiter ";"
    char *token = strtok(input, ";");
    int i = 0;
    // Stores the commands in the commands array
    while (token != NULL && i < MAX_ARGS - 1) {
        commands[i++] = token;
        token = strtok(NULL, ";");
    }
    commands[i] = NULL;
}

void parse_command(char *cmd, char **args) {
    // Splits the command into arguments using the delimiter " "
    char *token = strtok(cmd, " \n");
    int i = 0;
    // Stores the arguments in the args array
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \n");
    }
    args[i] = NULL;
}

/**
 * Function to initialize memory management structures
 */
void initialize_memory_management() {
    // Initialize frame table
    for (int i = 0; i < PHYSICAL_MEMORY_SIZE / PAGE_SIZE; i++) {
        frame_table[i].process_id = -1; // Set process ID to -1 (no process assigned)
        frame_table[i].page_number = -1; // Set page number to -1 (no page assigned)
        frame_table[i].valid = 0; // Set valid flag to 0 (not currently being used)
        page_access_history[i].access_time = 0; // Set access time to 0 (not accessed yet)
    }

    // Initialize page tables for each process with a 2D array 
    for (int i = 0; i < MAX_PROCESSES; i++) {
        for (int j = 0; j < VIRTUAL_MEMORY_SIZE / PAGE_SIZE; j++) {
            page_table[i][j].frame_number = -1; // Initialize frame number to -1 (no frame assigned)
            page_table[i][j].valid = 0; // Set valid flag to 0 (not currently being used)
        }
    }

    // Initialize kernel page table
    for (int i = 0; i < KERNEL_MEMORY_SIZE / PAGE_SIZE; i++) {
        kernel_page_table[i].frame_number = i; // Set frame number to i (kernel pages are contiguous)
        kernel_page_table[i].valid = 1; // Set valid flag to 1 (kernel pages are always valid)
    }
}

int calculate_required_pages(char **args) {
    if (strcmp(args[0], "ls") == 0) {
        return 1;
    } else if (strcmp(args[0], "gcc") == 0) {
        return 10;
    } else {
        return 5; // Default to 5 pages for other commands
    }
}

// Function to allocate a page in physical memory
int allocate_page(int process_id, int page_number, int is_kernel_page) {
    int oldest_frame_index = -1;
    int oldest_access_time = INT_MAX;

    // Find the least recently used frame
    for (int i = 0; i < PHYSICAL_MEMORY_SIZE / PAGE_SIZE; i++) {
        if (!frame_table[i].valid) {
            // If there's a free frame, use it
            oldest_frame_index = i;
            break;
        } else if (page_access_history[i].access_time < oldest_access_time) {
            oldest_frame_index = i;
            oldest_access_time = page_access_history[i].access_time;
        }
    }

    if (oldest_frame_index == -1) {
        printf(RED "Out of physical memory\n" RESET);
        return -1; // No free frames
    }

    // Update page table and frame table
    if (frame_table[oldest_frame_index].valid) {
        int old_process_id = frame_table[oldest_frame_index].process_id;
        int old_page_number = frame_table[oldest_frame_index].page_number;
        if (old_process_id == -1) {
            kernel_page_table[old_page_number].valid = 0;
        } else {
            page_table[old_process_id][old_page_number].valid = 0;
        }
    }

    if (is_kernel_page) {
        kernel_page_table[page_number].frame_number = oldest_frame_index;
        kernel_page_table[page_number].valid = 1;
    } else {
        page_table[process_id][page_number].frame_number = oldest_frame_index;
        page_table[process_id][page_number].valid = 1;
    }

    frame_table[oldest_frame_index].process_id = is_kernel_page ? -1 : process_id;
    frame_table[oldest_frame_index].page_number = page_number;
    frame_table[oldest_frame_index].valid = 1;

    page_access_history[oldest_frame_index].process_id = is_kernel_page ? -1 : process_id;
    page_access_history[oldest_frame_index].page_number = page_number;
    page_access_history[oldest_frame_index].access_time = current_time++;

    return 0;
}

/**
 * Function to deallocate memory for a process
 */
void deallocate_memory(int process_id) {
    for (int i = 0; i < VIRTUAL_MEMORY_SIZE / PAGE_SIZE; i++) {
        if (page_table[process_id][i].valid) {
            int frame_number = page_table[process_id][i].frame_number;
            // Deallocate frame in frame table
            frame_table[frame_number].process_id = -1;
            frame_table[frame_number].page_number = -1;
            frame_table[frame_number].valid = 0;
            // Reset access time in page access history
            page_access_history[frame_number].access_time = 0;

            // Deallocate page in page table
            page_table[process_id][i].frame_number = -1;
            page_table[process_id][i].valid = 0;
        }
    }
}

//this function is called when a page fault occurs and a page needs to be allocated
void handle_page_fault(int process_id, int page_number, int is_kernel_page) {
    if (allocate_page(process_id, page_number, is_kernel_page) == -1) {
        printf(RED "Failed to allocate memory for page %d of process %d\n" RESET, page_number, process_id);
    } else {
        for (int i = 0; i < PHYSICAL_MEMORY_SIZE / PAGE_SIZE; i++) {
            if ((frame_table[i].process_id == process_id && frame_table[i].page_number == page_number) ||
                (frame_table[i].process_id == -1 && frame_table[i].page_number == page_number)) {
                page_access_history[i].access_time = current_time++;
                break;
            }
        }
    }
}


void enqueue(Queue* queue, Process* process) {
    printf("Enqueuing process PID: %d, Priority: %d\n", process->pid, process->priority);
    if (queue->head == NULL || queue->head->priority > process->priority) {
        process->next = queue->head;
        queue->head = process;
        if (queue->tail == NULL) {
            queue->tail = process;
        }
        printf("Inserted at the head\n");
    } else {
        Process* current = queue->head;
        while (current->next != NULL && current->next->priority <= process->priority) {
            current = current->next;
        }
        process->next = current->next;
        current->next = process;
        if (process->next == NULL) {
            queue->tail = process;
        }
        printf("Inserted after PID: %d\n", current->pid);
    }
}



Process* dequeue(Queue* queue) {
    if (queue->head == NULL) return NULL;
    Process* temp = queue->head;
    queue->head = queue->head->next;
    if (queue->head == NULL) queue->tail = NULL;
    return temp;
}


void addProcess(int pid, const char* command, int burst_time, int io_time, int priority) {
    if (process_count >= MAX_PROCESSES) {
        printf("Maximum process limit reached.\n");
        return;
    }
    Process* process = (Process*)malloc(sizeof(Process));
    process->pid = pid;
    process->burst_time = burst_time;
    process->remaining_time = burst_time;
    process->io_time = io_time;
    process->priority = priority;
    process->state = READY;

    // Remove quotes from command
    char cmd_no_quotes[256];
    int j = 0;
    for (int i = 0; i < strlen(command); i++) {
        if (command[i] != '"') {
            cmd_no_quotes[j++] = command[i];
        }
    }
    cmd_no_quotes[j] = '\0';
    strcpy(process->command, cmd_no_quotes);

    process->next = NULL;
    processes[process_count++] = process;
    enqueue(&readyQueue, process);
    printf("Added process PID: %d, Command: %s, Burst Time: %d, IO Time: %d, Priority: %d\n", pid, process->command, burst_time, io_time, priority);
}

const char* stateToString(State state) {
    switch (state) {
        case NEW: return "NEW";
        case READY: return "READY";
        case RUN: return "RUN";
        case WAIT: return "WAIT";
        case TERMINATED: return "TERMINATED";
        default: return "UNKNOWN";
    }
}

void printProcessInfo(Process* process, int detailed) {
    if (detailed) {
        printf("PID: %-4d | State: %-10s | Command: %-20s | Burst Time: %-5d | Remaining Time: %-5d | IO Time: %-5d | Priority: %-3d\n",
               process->pid, stateToString(process->state), process->command, process->burst_time, process->remaining_time, process->io_time, process->priority);
    } else {
        printf("PID: %-4d | State: %-10s | Command: %-20s\n", process->pid, stateToString(process->state), process->command);
    }
}


void printAllProcesses(int detailed, int sorted_by_id) {
    if (sorted_by_id) {
        for (int i = 0; i < process_count - 1; i++) {
            for (int j = 0; j < process_count - i - 1; j++) {
                if (processes[j]->pid > processes[j + 1]->pid) {
                    Process* temp = processes[j];
                    processes[j] = processes[j + 1];
                    processes[j + 1] = temp;
                }
            }
        }
    }
    for (int i = 0; i < process_count; i++) {
        printProcessInfo(processes[i], detailed);
    }
}

void changeProcessPriority(int pid, int priority) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i]->pid == pid) {
            processes[i]->priority = priority;
            printf("Priority of process %d changed to %d\n", pid, priority);
            return;
        }
    }
    printf("Process with PID %d not found\n", pid);
}

#define TIME_QUANTUM 5  // Define a time quantum for the round-robin scheduler

void FCFS() {
    while (readyQueue.head != NULL) {
        Process* process = dequeue(&readyQueue);
        if (process == NULL) continue;

        process->state = RUN;
        printf("\nRunning process %d (Command: %s)\n", process->pid, process->command);

        int pid = fork();
        if (pid == 0) {
            char* args[MAX_ARGS];
            parse_command(process->command, args);
            execvp(args[0], args);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            int status;
            waitpid(pid, &status, 0);  // Wait for the process to complete
            if (WIFEXITED(status)) {
                process->state = TERMINATED;
                printf("Process %d terminated\n", process->pid);
            }
        }
    }
}






void execute_command(char **args) {
    int required_pages = calculate_required_pages(args);

    // Fork the process
    child_pid = fork();
    if (child_pid < 0) {
        perror(RED "Fork failed" RESET);
        exit(1);
    } else if (child_pid == 0) {
        // Child process
        int process_id = getpid() % MAX_PROCESSES; // Get the child process ID

        // Allocate memory for the process
        for (int i = 0; i < required_pages; i++) {
            if (page_table[process_id][i].valid == 0) {
                handle_page_fault(process_id, i, 0);
            }
        }

        // Execute the command
        if (execvp(args[0], args) == -1) {
            perror(RED "Exec failed" RESET);
            exit(1);
        }
        deallocate_memory(process_id); // <-- Add this line

    } else {
        // Parent process
        int status;
        waitpid(child_pid, &status, 0);
        child_pid = -1; // Reset child_pid after command execution
    }
}

void handleCommand(char* command) {
    char *args[MAX_ARGS];
    if (strncmp(command, "procs", 5) == 0) {
        int detailed = 0, sorted_by_id = 0;
        if (strstr(command, "-a")) detailed = 1;
        if (strstr(command, "-si")) sorted_by_id = 1;
        printAllProcesses(detailed, sorted_by_id);
    } else if (strncmp(command, "info", 4) == 0) {
        int pid;
        sscanf(command, "info %d", &pid);
        for (int i = 0; i < process_count; i++) {
            if (processes[i]->pid == pid) {
                printProcessInfo(processes[i], 1);
                return;
            }
        }
        printf("Process with PID %d not found\n", pid);
    } else if (strncmp(command, "priority", 8) == 0) {
        int pid, priority;
        sscanf(command, "priority %d %d", &pid, &priority);
        changeProcessPriority(pid, priority);
    } else if (strncmp(command, "run", 3) == 0) {
            FCFS();  // Use FCFS instead of Round Robin
    } else if (strncmp(command, "add", 3) == 0) {
        int pid, burst_time, io_time, priority;
        char cmd[256];
        sscanf(command, "add %d %d %d %d %[^\n]", &pid, &burst_time, &io_time, &priority, cmd);
        addProcess(pid, cmd, burst_time, io_time, priority);
    } else {
        parse_command(command, args);
        execute_command(args);
    }
}



void interactiveMode() {
    char line[MAX_LINE];
    char *commands[MAX_ARGS];
    while (1) {
        printf(PURPLE "GLShell> " RESET);
        if (!fgets(line, sizeof(line), stdin)) {
            break; // Handle EOF
        }
        split_commands(line, commands);
        for (int i = 0; commands[i] != NULL; i++) {
            log_command(commands[i]); // Log the command

            handleCommand(commands[i]);
        }
    }
}

void batchMode(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Could not open batch file");
        return;
    }
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;  // Remove newline character
        if (strspn(line, " \t\r\n") == strlen(line)) continue;

        // Skip empty lines or lines with only whitespace
        // printf("Executing command from batch file: %s\n", line);  // Debugging statement
        handleCommand(line);
    }
    fclose(file);
}

int main(int argc, char *argv[]) {
    // Initialize memory management
    initialize_memory_management();

    // Setting up signal handlers for SIGINT and SIGQUIT
    signal(SIGINT, exit_shell);
    signal(SIGQUIT, end_execution);

    readyQueue.head = readyQueue.tail = NULL;

    if (argc == 2) {
        batchMode(argv[1]);
    } else {
        interactiveMode();
    }

    return 0;
}
