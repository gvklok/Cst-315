#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>

// ANSI color codes
#define RESET "\033[0m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define PURPLE "\033[35m"

// Memory definitions
#define PHYSICAL_MEMORY_SIZE (16 * 1024 * 1024) // 16 MB
#define VIRTUAL_MEMORY_SIZE (64 * 1024 * 1024)  // 64 MB
#define KERNEL_MEMORY_SIZE (4 * 1024 * 1024)    // 4 MB
#define PAGE_SIZE 4096                          // 4 KB
#define MAX_PROCESSES 64
#define MAX_LINE 1024
#define MAX_ARGS 64

typedef struct { int frame_number, valid; } PageTableEntry;
typedef struct { int process_id, page_number, valid; } FrameTableEntry;
typedef struct { int process_id, page_number, access_time; } PageAccessEntry;
typedef enum { NEW, READY, RUN, WAIT, TERMINATED } State;
typedef struct Process {
    int pid, burst_time, remaining_time, io_time, priority;
    State state;
    char command[256];
    struct Process* next;
} Process;
typedef struct { Process* head, * tail; } Queue;

PageTableEntry page_table[MAX_PROCESSES][VIRTUAL_MEMORY_SIZE / PAGE_SIZE], kernel_page_table[KERNEL_MEMORY_SIZE / PAGE_SIZE];
FrameTableEntry frame_table[PHYSICAL_MEMORY_SIZE / PAGE_SIZE];
PageAccessEntry page_access_history[PHYSICAL_MEMORY_SIZE / PAGE_SIZE];
Queue readyQueue;
Process* processes[MAX_PROCESSES];
int process_count = 0, current_time = 0, free_frame_index = 0;
pid_t child_pid = -1;

void log_command(const char* command) {
    FILE* history_file = fopen("History.txt", "a");
    if (history_file) { fprintf(history_file, "%s\n", command); fclose(history_file); }
    else perror(RED "Failed to open history file" RESET);
}

void exit_shell(int sig) { printf(RED "\nExiting shell...\n" RESET); exit(0); }
void end_execution(int sig) { if (child_pid > 0) { kill(child_pid, SIGKILL); printf(YELLOW "\nCommand interrupted. Returning to prompt...\n" RESET); } }

void split_commands(char* input, char** commands) {
    char* token = strtok(input, ";"); int i = 0;
    while (token && i < MAX_ARGS - 1) { commands[i++] = token; token = strtok(NULL, ";"); }
    commands[i] = NULL;
}

void parse_command(char* cmd, char** args) {
    char* token = strtok(cmd, " \n"); int i = 0;
    while (token && i < MAX_ARGS - 1) { args[i++] = token; token = strtok(NULL, " \n"); }
    args[i] = NULL;
}

void initialize_memory_management() {
    for (int i = 0; i < PHYSICAL_MEMORY_SIZE / PAGE_SIZE; i++) {
        frame_table[i].process_id = -1; frame_table[i].page_number = -1; frame_table[i].valid = 0;
        page_access_history[i].access_time = 0;
    }
    for (int i = 0; i < MAX_PROCESSES; i++)
        for (int j = 0; j < VIRTUAL_MEMORY_SIZE / PAGE_SIZE; j++)
            page_table[i][j].frame_number = -1, page_table[i][j].valid = 0;
    for (int i = 0; i < KERNEL_MEMORY_SIZE / PAGE_SIZE; i++)
        kernel_page_table[i].frame_number = i, kernel_page_table[i].valid = 1;
}

int allocate_page(int process_id, int page_number, int is_kernel_page) {
    int oldest_frame_index = -1, oldest_access_time = INT_MAX;
    for (int i = 0; i < PHYSICAL_MEMORY_SIZE / PAGE_SIZE; i++) {
        if (!frame_table[i].valid) { oldest_frame_index = i; break; }
        else if (page_access_history[i].access_time < oldest_access_time) {
            oldest_frame_index = i; oldest_access_time = page_access_history[i].access_time;
        }
    }
    if (oldest_frame_index == -1) { printf(RED "Out of physical memory\n" RESET); return -1; }
    if (frame_table[oldest_frame_index].valid) {
        int old_process_id = frame_table[oldest_frame_index].process_id, old_page_number = frame_table[oldest_frame_index].page_number;
        if (old_process_id == -1) kernel_page_table[old_page_number].valid = 0;
        else page_table[old_process_id][old_page_number].valid = 0;
    }
    if (is_kernel_page) kernel_page_table[page_number].frame_number = oldest_frame_index, kernel_page_table[page_number].valid = 1;
    else page_table[process_id][page_number].frame_number = oldest_frame_index, page_table[process_id][page_number].valid = 1;
    frame_table[oldest_frame_index].process_id = is_kernel_page ? -1 : process_id;
    frame_table[oldest_frame_index].page_number = page_number; frame_table[oldest_frame_index].valid = 1;
    page_access_history[oldest_frame_index].process_id = is_kernel_page ? -1 : process_id;
    page_access_history[oldest_frame_index].page_number = page_number;
    page_access_history[oldest_frame_index].access_time = current_time++;
    return 0;
}

void deallocate_memory(int process_id) {
    for (int i = 0; i < VIRTUAL_MEMORY_SIZE / PAGE_SIZE; i++) {
        if (page_table[process_id][i].valid) {
            int frame_number = page_table[process_id][i].frame_number;
            frame_table[frame_number].process_id = -1;
            frame_table[frame_number].page_number = -1;
            frame_table[frame_number].valid = 0;
            page_access_history[frame_number].access_time = 0;
            page_table[process_id][i].frame_number = -1;
            page_table[process_id][i].valid = 0;
        }
    }
}

void handle_page_fault(int process_id, int page_number, int is_kernel_page) {
    if (allocate_page(process_id, page_number, is_kernel_page) == -1)
        printf(RED "Failed to allocate memory for page %d of process %d\n" RESET, page_number, process_id);
    else {
        for (int i = 0; i < PHYSICAL_MEMORY_SIZE / PAGE_SIZE; i++) {
            if ((frame_table[i].process_id == process_id && frame_table[i].page_number == page_number) ||
                (frame_table[i].process_id == -1 && frame_table[i].page_number == page_number)) {
                page_access_history[i].access_time = current_time++; break;
            }
        }
    }
}

void enqueue(Queue* queue, Process* process) {
    printf("Enqueuing process PID: %d, Priority: %d\n", process->pid, process->priority);
    if (queue->head == NULL || queue->head->priority > process->priority) {
        // If the queue is empty or the process has higher priority than the head
        process->next = queue->head;
        queue->head = process;
        if (queue->tail == NULL) {
            queue->tail = process;
        }
        printf("Inserted at the head\n");
    } else {
        // Find the appropriate position to insert the process based on priority
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
    // Check if the maximum process limit has been reached
    if (process_count >= MAX_PROCESSES) {
        printf("Maximum process limit reached.\n");
        return;
    }

    // Allocate memory for the new process
    Process* process = (Process*)malloc(sizeof(Process));
    process->pid = pid;
    process->burst_time = burst_time;
    process->remaining_time = burst_time;
    process->io_time = io_time;
    process->priority = priority;
    process->state = READY;

    // Remove quotes from the command string
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

    // Print information about the added process
    printf("Added process PID: %d, Command: %s, Burst Time: %d, IO Time: %d, Priority: %d\n", pid, process->command, burst_time, io_time, priority);
}

const char* stateToString(State state) {
    switch (state) {
        case NEW: return "NEW"; case READY: return "READY"; case RUN: return "RUN";
        case WAIT: return "WAIT"; case TERMINATED: return "TERMINATED"; default: return "UNKNOWN";
    }
}

void printProcessInfo(Process* process, int detailed) {
    if (detailed)
        printf("PID: %-4d | State: %-10s | Command: %-20s | Burst Time: %-5d | Remaining Time: %-5d | IO Time: %-5d | Priority: %-3d\n",
               process->pid, stateToString(process->state), process->command, process->burst_time, process->remaining_time, process->io_time, process->priority);
    else printf("PID: %-4d | State: %-10s | Command: %-20s\n", process->pid, stateToString(process->state), process->command);
}

void printAllProcesses(int detailed, int sorted_by_id) {
    if (sorted_by_id)
        for (int i = 0; i < process_count - 1; i++)
            for (int j = 0; j < process_count - i - 1; j++)
                if (processes[j]->pid > processes[j + 1]->pid) {
                    Process* temp = processes[j]; processes[j] = processes[j + 1]; processes[j + 1] = temp;
                }
    for (int i = 0; i < process_count; i++) printProcessInfo(processes[i], detailed);
}

void changeProcessPriority(int pid, int priority) {
    for (int i = 0; i < process_count; i++) {
        if (processes[i]->pid == pid) { processes[i]->priority = priority;
            printf("Priority of process %d changed to %d\n", pid, priority); return; }
    }
    printf("Process with PID %d not found\n", pid);
}

void simulateFCFS() {
    while (readyQueue.head != NULL) {
        Process* process = dequeue(&readyQueue); if (process == NULL) continue;
        process->state = RUN; printf("\nRunning process %d (Command: %s)\n", process->pid, process->command);
        int pid = fork(); if (pid == 0) {
            char* args[MAX_ARGS]; parse_command(process->command, args);
            execvp(args[0], args); perror("execvp"); exit(EXIT_FAILURE);
        } else { int status; waitpid(pid, &status, 0); if (WIFEXITED(status)) process->state = TERMINATED; }
    }
}

void execute_command(char** args) {
    int required_pages = (strcmp(args[0], "ls") == 0) ? 1 : (strcmp(args[0], "gcc") == 0) ? 10 : 5;
    child_pid = fork();
    if (child_pid < 0) { perror(RED "Fork failed" RESET); exit(1); }
    else if (child_pid == 0) {
        int process_id = getpid() % MAX_PROCESSES;
        for (int i = 0; i < required_pages; i++)
            if (page_table[process_id][i].valid == 0) handle_page_fault(process_id, i, 0);
        execvp(args[0], args); perror(RED "Exec failed" RESET); exit(1);
        deallocate_memory(process_id);
    } else { int status; waitpid(child_pid, &status, 0); child_pid = -1; }
}

// File and Directory Operations
void create_file(const char* filename, int size) {
    FILE* file = fopen(filename, "w"); // Opens a file for writing
    if (!file) { // If the file cannot be opened
        perror(RED "Failed to create file" RESET); // Print an error message
        return; // Exit the function
    }
    if (size > 0) { // If the specified size is greater than 0
        char* buffer = (char*)malloc(size); // Allocate memory for the buffer
        if (!buffer) { // If memory allocation fails
            perror(RED "Failed to allocate memory" RESET); // Print an error message
            fclose(file); // Close the file
            return; // Exit the function
        }
        for (int i = 0; i < size; i++) { // Fill the buffer with random data
            buffer[i] = 'A' + (rand() % 26); // Randomly generate a character
        }
        fwrite(buffer, 1, size, file); // Write the buffer to the file
        free(buffer); // Free the allocated memory
    }
    fclose(file); // Close the file
    printf("Created file: %s with size: %d bytes\n", filename, size); // Print a success message
}

void rename_file(const char* oldname, const char* newname) {
    if (rename(oldname, newname) == -1) perror(RED "Failed to rename file" RESET);
    else printf("Renamed file: %s to %s\n", oldname, newname);
}

void delete_file(const char* filename) {
    if (unlink(filename) == -1) { // Attempt to delete the file
        perror(RED "Failed to delete file" RESET); // If deletion fails, print an error message
    } else {
        printf("Deleted file: %s\n", filename); // If successful, print a success message
    }
}


void move_file(const char* src, const char* dest) {
    if (rename(src, dest) == -1) { // Attempt to move the file
        perror(RED "Failed to move file" RESET); // If moving fails, print an error message
    } else {
        printf("Moved file: %s to %s\n", src, dest); // If successful, print a success message
    }
}


void duplicate_file(const char* src, const char* dest) {
    FILE* src_file = fopen(src, "r"); // Open the source file for reading
    if (!src_file) { // If the source file cannot be opened
        perror(RED "Failed to open source file" RESET); // Print an error message
        return; // Exit the function
    }
    FILE* dest_file = fopen(dest, "w"); // Open the destination file for writing
    if (!dest_file) { // If the destination file cannot be opened
        perror(RED "Failed to open destination file" RESET); // Print an error message
        fclose(src_file); // Close the source file
        return; // Exit the function
    }
    char buffer[4096]; // Buffer to hold file data
    size_t bytes; // Number of bytes read/written
    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0) { // Read from source file
        fwrite(buffer, 1, bytes, dest_file); // Write to destination file
    }
    fclose(src_file); // Close the source file
    fclose(dest_file); // Close the destination file
    printf("Duplicated file: %s to %s\n", src, dest); // Print a success message
}


void create_directory(const char* dirname) {
    if (mkdir(dirname, 0755) == -1) perror(RED "Failed to create directory" RESET);
    else printf("Created directory: %s\n", dirname);
}

void rename_directory(const char* oldname, const char* newname) {
    if (rename(oldname, newname) == -1) perror(RED "Failed to rename directory" RESET);
    else printf("Renamed directory: %s to %s\n", oldname, newname);
}

void delete_directory(const char* dirname) {
    DIR* dir = opendir(dirname); // Open the directory
    if (!dir) { // If the directory cannot be opened
        perror(RED "Failed to open directory" RESET); // Print an error message
        return; // Exit the function
    }
    struct dirent* entry; // Directory entry structure
    while ((entry = readdir(dir)) != NULL) { // Read each entry in the directory
        char path[PATH_MAX]; // Buffer to hold the path of the entry
        snprintf(path, sizeof(path), "%s/%s", dirname, entry->d_name); // Construct the path
        if (entry->d_type == DT_DIR) { // If the entry is a directory
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue; // Skip "." and ".."
            delete_directory(path); // Recursively delete the subdirectory
        } else {
            unlink(path); // Delete the file
        }
    }
    closedir(dir); // Close the directory
    if (rmdir(dirname) == -1) { // Attempt to remove the directory
        perror(RED "Failed to delete directory" RESET); // If removal fails, print an error message
    } else {
        printf("Deleted directory: %s\n", dirname); // If successful, print a success message
    }
}


void move_directory(const char* src, const char* dest) {
    if (rename(src, dest) == -1) perror(RED "Failed to move directory" RESET);
    else printf("Moved directory: %s to %s\n", src, dest);
}

void duplicate_directory(const char* src, const char* dest) {
    DIR* dir = opendir(src); // Open the source directory
    if (!dir) { // If the source directory cannot be opened
        perror(RED "Failed to open source directory" RESET); // Print an error message
        return; // Exit the function
    }
    if (mkdir(dest, 0755) == -1) { // Attempt to create the destination directory
        perror(RED "Failed to create destination directory" RESET); // If creation fails, print an error message
        closedir(dir); // Close the source directory
        return; // Exit the function
    }
    struct dirent* entry; // Directory entry structure
    while ((entry = readdir(dir)) != NULL) { // Read each entry in the source directory
        char src_path[PATH_MAX], dest_path[PATH_MAX]; // Buffers to hold the paths of the source and destination entries
        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name); // Construct the source path
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest, entry->d_name); // Construct the destination path
        if (entry->d_type == DT_DIR) { // If the entry is a directory
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue; // Skip "." and ".."
            duplicate_directory(src_path, dest_path); // Recursively duplicate the subdirectory
        } else {
            duplicate_file(src_path, dest_path); // Duplicate the file
        }
    }
    closedir(dir); // Close the source directory
    printf("Duplicated directory: %s to %s\n", src, dest); // Print a success message
}


void search_file(const char* dirname, const char* filename) {
    DIR* dir = opendir(dirname); // Open the directory
    if (!dir) { // If the directory cannot be opened
        perror(RED "Failed to open directory" RESET); // Print an error message
        return; // Exit the function
    }
    struct dirent* entry; // Directory entry structure
    while ((entry = readdir(dir)) != NULL) { // Read each entry in the directory
        char path[PATH_MAX]; // Buffer to hold the path of the entry
        snprintf(path, sizeof(path), "%s/%s", dirname, entry->d_name); // Construct the path
        if (entry->d_type == DT_DIR) { // If the entry is a directory
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue; // Skip "." and ".."
            search_file(path, filename); // Recursively search the subdirectory
        } else if (strcmp(entry->d_name, filename) == 0) { // If the entry is a file and matches the filename
            printf("Found file: %s\n", path); // Print the path of the file
        }
    }
    closedir(dir); // Close the directory
}


// Function to display the directory tree recursively
void display_directory_tree(const char* dirname, int depth) {
    DIR* dir = opendir(dirname); // Open the directory
    if (!dir) { // If the directory cannot be opened
        perror(RED "Failed to open directory" RESET); // Print an error message
        return; // Exit the function
    }
    struct dirent* entry; // Directory entry structure
    while ((entry = readdir(dir)) != NULL) { // Read each entry in the directory
        for (int i = 0; i < depth; i++) printf("  "); // Indentation based on depth
        printf("|-- %s\n", entry->d_name); // Print the name of the entry
        if (entry->d_type == DT_DIR) { // If the entry is a directory
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue; // Skip "." and ".."
            char path[PATH_MAX]; // Buffer to hold the path of the subdirectory
            snprintf(path, sizeof(path), "%s/%s", dirname, entry->d_name); // Construct the path of the subdirectory
            display_directory_tree(path, depth + 1); // Recursively call the function for the subdirectory
        }
    }
    closedir(dir); // Close the directory
}

// Function to get information about a file
void get_file_info(const char* filename, int detailed) {
    struct stat file_stat;
    if (stat(filename, &file_stat) == -1) { perror(RED "Failed to get file information" RESET); return; }
    printf("File: %s\n", filename);
    printf("Size: %lld bytes\n", (long long)file_stat.st_size);
    printf("Permissions: %o\n", file_stat.st_mode & 0777);
    printf("Last modified: %s", ctime(&file_stat.st_mtime));
    if (detailed) {
        printf("Device: %lld\n", (long long)file_stat.st_dev);
        printf("Inode: %lld\n", (long long)file_stat.st_ino);
        printf("Links: %lld\n", (long long)file_stat.st_nlink);
        printf("UID: %d\n", file_stat.st_uid);
        printf("GID: %d\n", file_stat.st_gid);
    }
}

// Function to get information about a directory
void get_directory_info(const char* dirname, int detailed) {
    DIR* dir = opendir(dirname);
    if (!dir) { perror(RED "Failed to open directory" RESET); return; }
    struct dirent* entry;
    int total_files = 0, total_dirs = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) total_dirs++;
        else total_files++;
    }
    closedir(dir);
    printf("Directory: %s\n", dirname);
    printf("Total files: %d\n", total_files);
    printf("Total directories: %d\n", total_dirs);
    if (detailed) display_directory_tree(dirname, 0);
}

void handleCommand(char* command) {
    char* args[MAX_ARGS];
    if (strncmp(command, "procs", 5) == 0) {
        // Print all processes
        int detailed = strstr(command, "-a") != NULL, sorted_by_id = strstr(command, "-si") != NULL;
        printAllProcesses(detailed, sorted_by_id);
    } else if (strncmp(command, "info", 4) == 0) {
        // Print process information
        int pid; sscanf(command, "info %d", &pid);
        for (int i = 0; i < process_count; i++) if (processes[i]->pid == pid) { printProcessInfo(processes[i], 1); return; }
        printf("Process with PID %d not found\n", pid);
    } else if (strncmp(command, "priority", 8) == 0) {
        // Change process priority
        int pid, priority; sscanf(command, "priority %d %d", &pid, &priority);
        changeProcessPriority(pid, priority);
    } else if (strncmp(command, "run", 3) == 0) {
        // Simulate FCFS scheduling
        simulateFCFS();
    } else if (strncmp(command, "add", 3) == 0) {
        // Add a new process
        int pid, burst_time, io_time, priority; char cmd[256];
        sscanf(command, "add %d %d %d %d %[^\n]", &pid, &burst_time, &io_time, &priority, cmd);
        addProcess(pid, cmd, burst_time, io_time, priority);
    } else if (strncmp(command, "create_file", 11) == 0) {
        // Create a new file
        char filename[256]; int size;
        sscanf(command, "create_file %s %d", filename, &size);
        create_file(filename, size);
    } else if (strncmp(command, "rename_file", 11) == 0) {
        // Rename a file
        char oldname[256], newname[256];
        sscanf(command, "rename_file %s %s", oldname, newname);
        rename_file(oldname, newname);
    } else if (strncmp(command, "delete_file", 11) == 0) {
        // Delete a file
        char filename[256];
        sscanf(command, "delete_file %s", filename);
        delete_file(filename);
    } else if (strncmp(command, "move_file", 9) == 0) {
        // Move a file
        char src[256], dest[256];
        sscanf(command, "move_file %s %s", src, dest);
        move_file(src, dest);
    } else if (strncmp(command, "duplicate_file", 14) == 0) {
        // Duplicate a file
        char src[256], dest[256];
        sscanf(command, "duplicate_file %s %s", src, dest);
        duplicate_file(src, dest);
    } else if (strncmp(command, "create_dir", 10) == 0) {
        // Create a new directory
        char dirname[256];
        sscanf(command, "create_dir %s", dirname);
        create_directory(dirname);
    } else if (strncmp(command, "rename_dir", 10) == 0) {
        // Rename a directory
        char oldname[256], newname[256];
        sscanf(command, "rename_dir %s %s", oldname, newname);
        rename_directory(oldname, newname);
    } else if (strncmp(command, "delete_dir", 10) == 0) {
        // Delete a directory
        char dirname[256];
        sscanf(command, "delete_dir %s", dirname);
        delete_directory(dirname);
    } else if (strncmp(command, "move_dir", 8) == 0) {
        // Move a directory
        char src[256], dest[256];
        sscanf(command, "move_dir %s %s", src, dest);
        move_directory(src, dest);
    } else if (strncmp(command, "duplicate_dir", 13) == 0) {
        // Duplicate a directory
        char src[256], dest[256];
        sscanf(command, "duplicate_dir %s %s", src, dest);
        duplicate_directory(src, dest);
    } else if (strncmp(command, "search_file", 11) == 0) {
        // Search for a file in a directory
        char dirname[256], filename[256];
        sscanf(command, "search_file %s %s", dirname, filename);
        search_file(dirname, filename);
    } else if (strncmp(command, "display_tree", 12) == 0) {
        // Display directory tree
        char dirname[256];
        sscanf(command, "display_tree %s", dirname);
        display_directory_tree(dirname, 0);
    } else if (strncmp(command, "file_info", 9) == 0) {
        // Get file information
        char filename[256];
        int detailed = strstr(command, "-d") != NULL;
        sscanf(command, "file_info %s", filename);
        get_file_info(filename, detailed);
    } else if (strncmp(command, "dir_info", 8) == 0) {
        // Get directory information
        char dirname[256];
        int detailed = strstr(command, "-d") != NULL;
        sscanf(command, "dir_info %s", dirname);
        get_directory_info(dirname, detailed);
    } else {
        // Parse and execute a command
        parse_command(command, args);
        execute_command(args);
    }
}

void interactiveMode() {
    char line[MAX_LINE], *commands[MAX_ARGS];
    while (1) {
        printf(PURPLE "GLShell> " RESET);
        if (!fgets(line, sizeof(line), stdin)) break;
        split_commands(line, commands);
        for (int i = 0; commands[i] != NULL; i++) {
            log_command(commands[i]);
            handleCommand(commands[i]);
        }
    }
}

void batchMode(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) { perror("Could not open batch file"); return; }
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = 0;
        if (strspn(line, " \t\r\n") == strlen(line)) continue;
        handleCommand(line);
    }
    fclose(file);
}

int main(int argc, char* argv[]) {
    initialize_memory_management();
    signal(SIGINT, exit_shell); signal(SIGQUIT, end_execution);
    readyQueue.head = readyQueue.tail = NULL;
    if (argc == 2) {
        // Batch mode
        batchMode(argv[1]);
    } else {
        // Interactive mode
        interactiveMode();
    }
    return 0;
}