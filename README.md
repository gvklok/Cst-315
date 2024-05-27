# Project 2: Unix/Linux Command Line Interpreter

This program is a simple shell implemented in C that supports interactive and batch modes. It allows users to execute commands, both single and multiple, separated by semicolons. The shell also provides functionality to interrupt the execution of a command using a specific key combination.

## Software Requirements

- Operating System: Unix/Linux
- Compiler: GCC (GNU Compiler Collection)
- Text Editor: Any text editor of your choice (e.g., Vim, Nano)

## Running the Program

### Interactive Mode

1. Open a terminal.
2. Navigate to the directory containing the compiled executable file (`Project2`).
3. Run the program using `./Project2`. or simply load it into your ide and press run

### Batch Mode

1. Create a text file containing a list of commands, each on a new line.
2. Save the file with a `.txt` extension (e.g., `batch.txt`).
3. Open a terminal.
4. Navigate to the directory containing the compiled executable file (`shell`).
5. Run the program using `./Project2 batch.txt`.

## Key Combinations

- Press `CTRL + C` to exit the shell.
- Press `CTRL + \` to end the execution of the current command and return to the prompt.

## Example Commands

- `ls -l`: List files and directories in long format.
- `echo "Hello, World!"`: Print "Hello, World!" to the console.

## Program Features

- Supports both interactive and batch modes.
- Allows execution of multiple commands separated by semicolons.
- Provides functionality to interrupt command execution.


