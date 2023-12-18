#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_COMMANDS 20
#define MAX_COMMAND_LENGTH 50
#define _POSIX_C_SOURCE 200809L

void standard_execute(char *commands[MAX_COMMANDS + 1], char *log_name);
void piped_execute(char *commands[MAX_COMMANDS + 1], char *log_name);

volatile sig_atomic_t sigint_rec = 0;
volatile sig_atomic_t sigterm_rec = 0;

void handle_signal(int sig)
{
    if (sig == SIGINT)
    {
        printf("SIGINT received.\n");
        sigint_rec = 1;
    }
    else if (sig == SIGTERM)
    {
        printf("SIGTERM received.\n");
        sigterm_rec = 1;
    }
}

void log_commands(char *filename, pid_t pid, char **commands, int end_index)
{

    FILE *log_file = fopen(filename, "a");
    if (log_file == NULL)
    {
        perror("shell");
        exit(EXIT_FAILURE);
    }

    fprintf(log_file, "PID: %d\n", pid);
    fprintf(log_file, "Command: ");
    for (int i = 0; commands[i] != NULL && end_index != i; i++)
    {

        if (strcmp(commands[i], "|"))
            fprintf(log_file, "%s ", commands[i]);
    }
    fprintf(log_file, "\n\n");

    fclose(log_file);
}

int main()
{

    char input[MAX_COMMAND_LENGTH * MAX_COMMANDS];
    char *commands[MAX_COMMANDS + 1];
    char *token;
    int num_commands = 0;
    time_t cur = time(NULL);
    struct tm *local = localtime(&cur);

    char filename[MAX_COMMAND_LENGTH];
    strftime(filename, sizeof(filename), "%Y%m%d-%H%M%S.log", local);

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("Error setting SIGINT signal handler");
        exit(1);
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1)
    {
        perror("Error setting SIGTERM signal handler");
        exit(1);
    }

    while (1)
    {
        printf("$ ");

        fgets(input, sizeof(input), stdin);

        input[strcspn(input, "\n")] = '\0'; // remove newline

        if (strcmp(input, ":q") == 0)
        { // exit shell if user enters ":q"

            break;
        }

        // parse input into commands
        token = strtok(input, " ");
        while (token != NULL)
        {
            commands[num_commands++] = token;
            token = strtok(NULL, " ");
        }
        commands[num_commands] = NULL; // set last element to NULL
        sigint_rec = 0;
        sigterm_rec = 0;

        // execute commands
        standard_execute(commands, filename);

        // reset resources
        for (int i = 0; i < num_commands; i++)
        {
            commands[i] = NULL;
        }
        num_commands = 0;
        memset(input, 0, sizeof(input));
    }

    return 0;
}

void piped_execute(char *commands[MAX_COMMANDS + 1], char *log_name)
{

    int num_pipes = 0;
    int pipe_fds[MAX_COMMANDS][2];
    int end_index = -1;

    // Count the number of pipes in commands
    for (int i = 0; commands[i] != NULL; i++)
    {
        if (strcmp(commands[i], "|") == 0)
        {
            end_index = i;
            num_pipes++;
        }
    }
    pid_t pid[num_pipes];

    int cmd_start = 0;
    for (int i = 0; i < 2; i++)
    {
        int cmd_end = cmd_start;
        while (commands[cmd_end] != NULL && strcmp(commands[cmd_end], "|") != 0)
        {
            cmd_end++;
        }

        if (i < num_pipes)
        {

            if (pipe(pipe_fds[i]) == -1)
            {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        pid[i] = fork();
        if (pid[i] == 0)
        { // child process
            if (sigint_rec || sigterm_rec)
            {
                exit(EXIT_SUCCESS);
            }
            if (i > 0)
            {                                           // read from previous pipe
                close(pipe_fds[i - 1][1]);              // close write end of previous pipe
                dup2(pipe_fds[i - 1][0], STDIN_FILENO); // replace stdin with read end of previous pipe
                close(pipe_fds[i - 1][0]);              // close original read end of previous pipe
            }

            if (i < num_pipes)
            {                                        // write to current pipe
                close(pipe_fds[i][0]);               // close read end of current pipe
                dup2(pipe_fds[i][1], STDOUT_FILENO); // replace stdout with write end of current pipe
                close(pipe_fds[i][1]);               // close original write end of current pipe
            }
            if (sigint_rec || sigterm_rec)
            {
                exit(EXIT_SUCCESS);
            }

            char *cmd[MAX_COMMANDS + 1] = {NULL};
            memcpy(cmd, &commands[cmd_start], sizeof(char *) * (cmd_end - cmd_start));

            if (execvp(cmd[0], cmd) == -1)
            {
                perror("piped_execute");
                exit(EXIT_FAILURE);
            }
            exit(EXIT_SUCCESS);
        }
        else if (pid < 0)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        cmd_start = cmd_end + 1;
    }

    // Parent
    for (int i = 0; i < num_pipes; i++)
    {
        close(pipe_fds[i][0]);
        close(pipe_fds[i][1]);
    }
    log_commands(log_name, pid[0], commands, end_index);
    log_commands(log_name, pid[1], commands + end_index, MAX_COMMANDS - 1);
    for (int i = 0; i < num_pipes + 1; i++)
    {
        wait(NULL);
    }
}

void standard_execute(char *commands[MAX_COMMANDS + 1], char *log_name)
{

    int num_pipes = 0;

    // Count the number of pipes
    for (int i = 0; commands[i] != NULL; i++)
    {
        if (strcmp(commands[i], "|") == 0)
        {
            num_pipes++;
        }
    }

    char *input_file = NULL;
    char *output_file = NULL;
    int input_fd = 0;
    int output_fd = 0;

    // Check for input and output redirections
    for (int i = 0; commands[i] != NULL; i++)
    {
        if (strcmp(commands[i], "<") == 0)
        {
            input_file = commands[i + 1];
            commands[i] = NULL;
        }
        else if (strcmp(commands[i], ">") == 0)
        {
            output_file = commands[i + 1];
            commands[i] = NULL;
        }
    }

    // Save the original file descriptors for stdin and stdout
    int orig_stdin = dup(STDIN_FILENO);
    int orig_stdout = dup(STDOUT_FILENO);

    // Redirect input if necessary
    if (input_file != NULL)
    {
        input_fd = open(input_file, O_RDONLY);
        if (input_fd == -1)
        {
            perror("open");
            exit(EXIT_FAILURE);
        }
        if (dup2(input_fd, STDIN_FILENO) == -1)
        {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(input_fd);
    }

    // Redirect output if necessary
    if (output_file != NULL)
    {
        output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (output_fd == -1)
        {
            perror("open");
            exit(EXIT_FAILURE);
        }
        if (dup2(output_fd, STDOUT_FILENO) == -1)
        {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(output_fd);
    }

    // Execute the command
    if (num_pipes == 0)
    {

        pid_t pid = fork();
        if (pid == 0)
        { // child process
            if (sigint_rec || sigterm_rec)
            {
                exit(EXIT_SUCCESS);
            }
            if (execvp(commands[0], commands) == -1)
            {
                perror("shell");
                exit(EXIT_FAILURE);
            }
            return;
        }
        else if (pid < 0)
        { // error
            perror("fork");
        }
        else
        { // parent process
            wait(NULL);
            log_commands(log_name, pid, commands, MAX_COMMANDS - 1);
        }
    }
    else
    {

        piped_execute(commands, log_name);
    }

    if (input_file != NULL)
    {
        // Restore the original file descriptors for stdin and stdout
        if (dup2(orig_stdin, STDIN_FILENO) == -1)
        {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
    }

    if (output_file != NULL)
    {
        if (dup2(orig_stdout, STDOUT_FILENO) == -1)
        {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
    }

    close(orig_stdin);
    close(orig_stdout);
}