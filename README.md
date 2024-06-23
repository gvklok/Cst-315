# Command Injection Malware and Detection

## Overview
This assignment involves creating a C program that demonstrates a command injection vulnerability and another program that detects such vulnerabilities. The malware program prompts the user to delete a `.txt` file but secretly deletes all `.txt` files in the current directory. The detection program scans for patterns indicative of command injection.

## Files Included
- `injection.c`: The malware program demonstrating command injection.
- `checkForVirus.c`: The detection program that scans for command injection vulnerabilities.

## How to Compile
To compile the programs, use the following commands:

```sh
gcc -o injection injection.c
gcc -o checkForVirus checkForVirus.c
```

## How to Run

### Malware Program
1. Run the malware program:
    ```sh
    ./injection
    ```
2. Enter the name of a `.txt` file to delete when prompted.
3. The program will pretend to delete the specified file but will actually delete all `.txt` files in the current directory.

### Detection Program
1. Run the detection program:
    ```sh
    ./checkForVirus
    ```
2. The program will scan the current directory for files containing suspicious patterns indicative of command injection.
3. If any suspicious files are found, it will print a warning. If no suspicious files are found, it will print a message indicating that.

## Description of the Programs

### Malware Program (`injection.c`)
The malware program performs the following steps:
1. Prompts the user to enter the name of a `.txt` file to delete.
2. Checks if the provided filename has a `.txt` extension.
3. Pretends to delete the specified file by printing a message.
4. Secretly deletes all `.txt` files in the current directory by iterating through the directory entries and using the `unlink` function.

### Detection Program (`checkForVirus.c`)
The detection program performs the following steps:
1. Gets the current working directory.
2. Opens the current directory and iterates through its entries.
3. Checks each file for patterns indicating potential command injection (e.g., the use of `unlink`, `opendir`, and `readdir`).
4. Prints a warning if any suspicious files are found. If no suspicious files are found, it prints a message indicating that.

