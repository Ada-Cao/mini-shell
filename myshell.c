#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

#define PROMPT_LEN 100
#define MAX_ARGS 64
#define hist_size 1024
int dir_changed = 0;  // record if the directory has been changed
int head = 0, filled = 0;
char *hist[hist_size];


// 1-print my prompt
void printPrompt()
{
    int offset;
    char tmpStr[100];

    // header {CAO}
    printf("\033[38;5;117m\033[1m{CAO}\033[m"); // @color #87d7ff

    // [username@hostname home/seu/Desktop] 
    offset = 0;
    tmpStr[offset++] = '[';

    struct passwd *pw = getpwuid(geteuid()); // get username
    strcpy(tmpStr + offset, pw->pw_name);
    offset += strlen(tmpStr + offset);
    tmpStr[offset++] = '@';
    gethostname(tmpStr + offset, PROMPT_LEN - offset); // get hostname
    offset += strlen(tmpStr + offset);
    tmpStr[offset++] = ' ';
    getcwd(tmpStr + offset, PROMPT_LEN - offset); // get current directory
    offset += strlen(tmpStr + offset);
    tmpStr[offset++] = ']';
    tmpStr[offset] = '\0';
    printf("\033[38;5;141m\033[1m%s\033[m", tmpStr); /// @color #af87ff

    // print prompt symbol
    printf((!geteuid()) ? "# " : "$ ");
}


// 2-parse user's input 
void parse(char *command, char **argv)
{
    int count = 0; // record the number of arguments
    char *p;       // point to the currently parsed argument
    char *saveptr; // save parsing state
    memset(argv, 0, sizeof(char *) * MAX_ARGS);

    // Split the command string by space or tab
    p = strtok_r(command, " \t", &saveptr);
    while (p != NULL)
    {
        // handle "" or '', remove the quotes
        if ((p[0] == '"' && p[strlen(p)-1] == '"') || (p[0] == '\'' && p[strlen(p)-1] == '\''))
        {
            p[strlen(p)-1] = '\0';
            argv[count++] = p + 1;
        }
        else
        {
            argv[count++] = p;
        }


        if (count >= MAX_ARGS - 1)
            break;

        p = strtok_r(NULL, " \t", &saveptr);
    }
    argv[count] = NULL;

    // avoid NULL pointer crash
    if (argv[0] == NULL) return;

    // Handle built-in commands
    if (strcmp(argv[0], "exit") == 0)
        exit(0);
    else if (strcmp(argv[0], "cd") == 0)
    {
        if (argv[1] == NULL || strcmp(argv[1], "~") == 0)
        {
            struct passwd *pw = getpwuid(geteuid());
            if (pw == NULL)
                perror("getpwuid error");
            else if (chdir(pw->pw_dir) == -1)
                perror("cd error");
        }
        else // normal path
        {
            if (chdir(argv[1]) == -1)
                perror("cd error");
        }

        dir_changed = 1;
        return;
    }
}



// 3-get the first command : remove extra spaces
char *trim(char *string) 
{
    int i = 0;
    int j = 0;
    // char *ptr = malloc(sizeof(char *) * strlen(string));
    char *ptr = malloc(strlen(string) + 1); 

    for (i = 0; string[i] != '\0'; i++)
        if (string[i] != ' ')
        {
            ptr[j] = string[i];
            j++;
        }
    ptr[j] = '\0';
    string = ptr;

    return string;
}




// 4-execute the basic order
void execute(char **argv)
{
    pid_t pid;
    int status;
    if ((pid = fork()) < 0) 
    {
        printf("error:fork failed.\n");
        exit(1);
    }
    else if (pid == 0) // child process
    {
        if (execvp(argv[0], argv) < 0 && strcmp(argv[0], "cd"))
            printf("error:invalid command.\n");
        exit(0);
    }
    else // parent process
    {
        // wait for child process
        while (wait(&status) != pid)
            ;
    }
}


// 5-output redirect: > < |
void execute_file(char **argv, char *output)
{
    pid_t pid;
    int status, flag;
    char *file = NULL;

    if ((pid = fork()) < 0)
    {
        printf("error:fork failed.\n");
        exit(1);
    }
    else if (pid == 0) // child process
    {
        if (strstr(output, ">") > 0) //return a pointer to first occurence after > or null
        {
            char *p = strtok_r(output, ">", &file);
            output += 1;       //change2
            file = trim(file); //get the first command of file, that is to say, get the first command after >
            flag = 1;
            int old_stdout = dup(1);
            //output redirect
            FILE *fp1 = freopen(output, "w+", stdout);
            execute_file(argv, file);
            fclose(stdout);
            FILE *fp2 = fdopen(old_stdout, "w");
            *stdout = *fp2;
            exit(0);
        }
        if (strstr(output, "<") > 0)
        {
            char *p = strtok_r(output, "<", &file);
            file = trim(file);
            flag = 1;
            int fd = open(file, O_RDONLY);
            if (fd < 0)
            {
                printf("No such file or directory.");
                exit(0);
            }
        }
        if (strstr(output, "|") > 0)
        {
            fflush(stdout);
            printf("here");
            fflush(stdout);
            char *p = strtok_r(output, "|", &file);
            file = trim(file);
            flag = 1;
            char *args[64];
            parse(file, args);
            execute(args);
        }
        int old_stdout = dup(1);
        FILE *fp1 = freopen(output, "w+", stdout);
        if (execvp(argv[0], argv) < 0)
            printf("error:in exec\n");
        fclose(stdout);
        FILE *fp2 = fdopen(old_stdout, "w");
        *stdout = *fp2;
        exit(0);
    }
    else // parent process
    {
        while (wait(&status) != pid)
            ;
    }
}


// 6-input redirect: > < |
void execute_input(char **argv, char *output)
{
    pid_t pid;
    int fd;
    char *file;
    int flag = 0;
    int status;

    if ((pid = fork()) < 0)
    {
        printf("error:fork failed\n");
        exit(1);
    }
    else if (pid == 0) // child process
    {
        if (strstr(output, "<") > 0)
        {
            char *p = strtok_r(output, "<", &file);
            file = trim(file);
            flag = 1;
            fd = open(output, O_RDONLY);
            if (fd < 0)
            {
                printf("No such file or directory.");
                exit(0);
            }
            output = file;
        }
        if (strstr(output, ">") > 0)
        {
            char *p = strtok_r(output, ">", &file);
            file = trim(file);
            flag = 1;
            fflush(stdout);
            int old_stdout = dup(1);
            FILE *fp1 = freopen(file, "w+", stdout);
            execute_input(argv, output);
            fclose(stdout);
            FILE *fp2 = fdopen(old_stdout, "w");
            *stdout = *fp2;
            exit(0);
        }
        if (strstr(output, "|") > 0)
        {
            char *p = strtok_r(output, "|", &file);
            file = trim(file);
            flag = 1;
            char *args[64];
            parse(file, args);
            int pfds[2];
            pid_t pid, pid2;
            int status, status2;
            pipe(pfds); // create a pipe
            int fl = 0;

            if ((pid = fork()) < 0)
            {
                printf("error:fork failed\n");
                exit(1);
            }
            if ((pid2 = fork()) < 0)
            {
                printf("error:fork failed\n");
                exit(1);
            }

            if (pid == 0 && pid2 != 0)
            {
                close(1);
                dup(pfds[1]);
                close(pfds[0]);
                close(pfds[1]);
                fd = open(output, O_RDONLY);
                close(0);
                dup(fd);
                if (execvp(argv[0], argv) < 0)
                {
                    close(pfds[0]);
                    close(pfds[1]);
                    printf("error:in exec\n");
                    fl = 1;
                    exit(0);
                }
                close(fd);
                exit(0);
            }
            else if (pid2 == 0 && pid != 0 && fl != 1)
            {
                close(0);
                dup(pfds[0]);
                close(pfds[1]);
                close(pfds[0]);
                if (execvp(args[0], args) < 0)
                {
                    close(pfds[0]);
                    close(pfds[1]);
                    printf("error:in exec\n");
                    exit(0);
                }
            }
            else
            {
                close(pfds[0]);
                close(pfds[1]);
                while (wait(&status) != pid)
                    ;
                while (wait(&status2) != pid2)
                    ;
            }
            exit(0);
        }
        fd = open(output, O_RDONLY);
        close(0);
        dup(fd);
        if (execvp(argv[0], argv) < 0)
        {
            printf("error:in exec\n");
        }
        close(fd);
        exit(0);
    }
    else // parent process
    {
        while (wait(&status) != pid)
            ;
    }
}


// 7-implement pipe
void execute_pipe(char **argv, char *output)
{
    int pfds[2], pf[2], flag;
    char *file;
    pid_t pid, pid2, pid3;
    int status, status2, old_stdout;
    pipe(pfds); //create pipe
    //pfds[0]:read        pfds[1]:write
    int blah = 0;
    char *args[64];
    char *argp[64];
    int fl = 0;
    if ((pid = fork()) < 0)
    {
        printf("error:fork failed\n");
        exit(1);
    }
    if ((pid2 = fork()) < 0)
    {
        printf("error:fork failed\n");
        exit(1);
    }
    if (pid == 0 && pid2 != 0)
    {
        close(1);
        dup(pfds[1]);
        close(pfds[0]);
        close(pfds[1]);
        if (execvp(argv[0], argv) < 0) //run the command
        {
            close(pfds[0]);
            close(pfds[1]);
            printf("error:in exec\n");
            fl = 1;
            kill(pid2, SIGUSR1);
            exit(0);
        }
    }
    else if (pid2 == 0 && pid != 0)
    {
        if (fl == 1)
        {
            exit(0);
        }
        if (strstr(output, "<") > 0)
        {
            char *p = strtok_r(output, "<", &file);
            file = trim(file);
            flag = 1;
            parse(output, args); //divide output to the array args
            execute_input(args, file);
            close(pfds[0]);
            close(pfds[1]);
            exit(0);
        }
        if (strstr(output, ">") > 0)
        {
            char *p = strtok_r(output, ">", &file);
            file = trim(file);
            flag = 1;
            parse(output, args);
            blah = 1;
        }

        else
        {
            parse(output, args);
        }
        close(0);
        dup(pfds[0]);
        close(pfds[1]);
        close(pfds[0]);
        if (blah == 1)
        {
            old_stdout = dup(1);
            FILE *fp1 = freopen(file, "w+", stdout);
        }
        if (execvp(args[0], args) < 0)
        {
            fflush(stdout);
            printf("error:in exec %d\n", pid);
            kill(pid, SIGUSR1);
            close(pfds[0]);
            close(pfds[1]);
        }
        fflush(stdout);
        printf("HERE");
        if (blah == 1)
        {
            fclose(stdout);
            FILE *fp2 = fdopen(old_stdout, "w");
            *stdout = *fp2;
        }
    }
    else
    {
        close(pfds[0]);
        close(pfds[1]);
        while (wait(&status) != pid)
            ;
        while (wait(&status2) != pid2)
            ;
    }
}


// 8-implement pipe
void execute_pipe2(char **argv, char **args, char **argp)
{
    int status;
    int i;
    int pipes[4];
    pipe(pipes);
    pipe(pipes + 2);

    if (fork() == 0) // child process
    {
        dup2(pipes[1], 1);
        close(pipes[0]);
        close(pipes[1]);
        close(pipes[2]);
        close(pipes[3]);
        if (execvp(argv[0], argv) < 0)
        {
            fflush(stdout);
            printf("error:in exec\n");
            fflush(stdout);
            close(pipes[0]);
            close(pipes[1]);
            close(pipes[2]);
            close(pipes[3]);
            exit(1);
        }
    }
    else // parent process
    {
        if (fork() == 0)
        {
            dup2(pipes[0], 0);
            dup2(pipes[3], 1);
            close(pipes[0]);
            close(pipes[1]);
            close(pipes[2]);
            close(pipes[3]);
            if (execvp(args[0], args) < 0)
            {
                fflush(stdout);
                printf("error:in exec\n");
                fflush(stdout);
                close(pipes[0]);
                close(pipes[1]);
                close(pipes[2]);
                close(pipes[3]);
                exit(1);
            }
        }
        else
        {
            if (fork() == 0)
            {
                dup2(pipes[2], 0);
                close(pipes[0]);
                close(pipes[1]);
                close(pipes[2]);
                close(pipes[3]);
                if (execvp(argp[0], argp) < 0)
                {
                    fflush(stdout);
                    printf("error:in exec\n");
                    fflush(stdout);
                    close(pipes[0]);
                    close(pipes[1]);
                    close(pipes[2]);
                    close(pipes[3]);
                    exit(1);
                }
            }
        }
    }
    close(pipes[0]);
    close(pipes[1]);
    close(pipes[2]);
    close(pipes[3]);
    for (i = 0; i < 3; i++)
        wait(&status);
}



int main()
{
    char line[1024];
    char *argv[64];
    char *args[64];
    char *left;
    size_t size = 0;
    char ch;
    int count = 0;
    char *tri;
    char *second;
    char *file;
    int i;
    for (i = 0; i < hist_size; i++)
    {
        hist[i] = (char *)malloc(150);
    }

    while (1)
    {
        int i = 0; /// for loop
        count = 0;
        int flag = 0;
        int len = 0;          /// length of the complete command
        char *command = NULL; /// the complete command
        char *dire[] = {"pwd"};
        fflush(stdout);
        printPrompt();

        len = getline(&command, &size, stdin); /// read line

        if (*command == '\n') /// just press enter
            continue;

        command[len - 1] = '\0'; /// replace \n with \0

        char *file = NULL;

        char *tempCommand = (char *)malloc(150);
        strcpy(tempCommand, command);
        parse(tempCommand, argv); /// split the command by space

        // If parse handled a builtin 'cd', skip the rest of processing for this line.
        if (dir_changed) {
            dir_changed = 0;       // reset flag
            free(tempCommand);     // free temp buffer
            continue;              // skip further parsing/execution for this command
        }

        strcpy(hist[(head + 1) % hist_size], command); //storing an entry in history
        head = (head + 1) % hist_size;
        filled = filled + 1;

        for (i = 0; command[i] != '\0'; i++)
        {

            if (command[i] == '>')
            {
                char *p = strtok_r(command, ">", &file);
                file = trim(file);
                flag = 1;
                break;
            }
            else if (command[i] == '<')
            {
                char *p = strtok_r(command, "<", &file);
                file = trim(file);
                flag = 2;
                break;
            }
            else if (command[i] == '|')
            {
                char *p = strtok_r(command, "|", &left);
                flag = 3;
                break;
            }
        }
        if (strcmp(command, "exit") == 0)
        {
            exit(0);
        }

        if (flag == 1)
        {
            parse(command, argv);
            execute_file(argv, file);
        }
        else if (flag == 2)
        {
            parse(command, argv);
            execute_input(argv, file);
        }
        else if (flag == 3)
        {
            char *argp[64];
            char *output, *file;
            if (strstr(left, "|") > 0)
            {
                char *p = strtok_r(left, "|", &file);
                parse(command, argv);
                parse(left, args);
                parse(file, argp);
                execute_pipe2(argv, args, argp);
            }
            else
            {
                parse(command, argv);
                execute_pipe(argv, left);
            }
        }
        else
        {
            parse(command, argv);
            execute(argv);
        }
    }
}
