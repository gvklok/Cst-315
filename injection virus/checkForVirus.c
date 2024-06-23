#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

// Function to check if a file contains suspicious patterns indicating command modification
int check_file_for_injection(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        perror("fopen");
        return 0;
    }

    char line[256];
    int found = 0;
    int unlink_found = 0;   // Flag to check if unlink function is found
    int dir_found = 0;      // Flag to check if directory operations are found
    int readdir_found = 0;  // Flag to check if readdir function is found

    // Read file line by line
    while (fgets(line, sizeof(line), file)) {
        // Look for patterns indicating command modification or directory traversal
        if (strstr(line, "unlink") != NULL) {
            unlink_found = 1;
        }
        if (strstr(line, "opendir") != NULL || strstr(line, "readdir") != NULL) {
            dir_found = 1;
            readdir_found = 1;
        }
        // If both unlink and directory traversal functions are found, flag as suspicious
        if (unlink_found && dir_found && readdir_found) {
            printf("Warning: file %s contains suspicious command modification!\n", filepath);
            found = 1;
            break;
        }
    }

    fclose(file);
    return found;
}

// Function to scan the current directory for files containing suspicious patterns
void scan_directory_for_injection(const char *source_name, const char *exe_name) {
    DIR *dir;
    struct dirent *entry;
    char cwd[1024];
    int found_any = 0;

    // Get the current working directory
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        return;
    }

    // Open the current directory
    dir = opendir(cwd);
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    // Loop through the directory entries
    while ((entry = readdir(dir)) != NULL) {
        // Check if the entry is a regular file
        if (entry->d_type == DT_REG) {
            // Skip the detection program's source and executable files
            if (strcmp(entry->d_name, source_name) == 0 || strcmp(entry->d_name, exe_name) == 0) {
                continue;
            }
            char filepath[1024];
            snprintf(filepath, sizeof(filepath), "%s/%s", cwd, entry->d_name);
            // Check if the file contains suspicious patterns
            if (check_file_for_injection(filepath)) {
                found_any = 1;
            }
        }
    }

    closedir(dir);

    // If no suspicious files were found, print a message
    if (!found_any) {
        printf("No suspicious files found in the directory.\n");
    }
}

int main(int argc, char *argv[]) {
    // Ensure the program name is passed as an argument
    if (argc < 1) {
        fprintf(stderr, "Usage: %s\n", argv[0]);
        return 1;
    }

    // Define the source and executable file names
    char source_file[] = "checkForVirus.c"; // Name of the source file
    char *exe_file = argv[0]; // Name of the executable file

    // Print the scanning message
    printf("Scanning current directory for injection virus...\n");

    // Scan the current directory for suspicious files
    scan_directory_for_injection(source_file, exe_file);

    return 0;
}
