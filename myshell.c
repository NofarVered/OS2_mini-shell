#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

void handle_sigchld(int sig)
{
    // cradit: http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
    int saved_errno = errno;
    while (waitpid((pid_t)(-1), 0, WNOHANG) > 0)
    {
    }
    errno = saved_errno;
}
void handler()
{
    // prevent zombies -SIG_CHLD
    struct sigaction sa_zmb;
    memset(&sa_zmb, 0, sizeof(sa_zmb));
    sa_zmb.sa_handler = &handle_sigchld;
    sa_zmb.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa_zmb, 0) != 0)
    {
        perror("ERROR - sigaction has failed");
        exit(1);
    }

    // the shell should not terminate upon SIGINT - SIG_IGN
    struct sigaction sa_ign;
    memset(&sa_ign, 0, sizeof(sa_ign));
    sa_ign.sa_handler = SIG_IGN;
    sa_ign.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa_ign, 0) != 0)
    {
        perror("ERROR - sigaction has failed");
        exit(1);
    }
}
void handler_dfl()
{
    //foreground child should terminate upon SIGINT -SIG_DFL
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, 0) == -1)
    {
        perror("ERROR - sigaction has failed");
        exit(1);
    }
}
int prepare()
{
    handler(0);
    return 0;
}

void background_process(char **command)
{
    // the parent NOT wait
    int pid = fork();
    if (pid == 0)
    {
        // son in the background
        execvp(command[0], command);
        perror("ERROR - son in the background");
        exit(1);
    }
    else if (pid == -1)
    {
        // parent faild
        perror("ERROR - parent faild");
        exit(1);
    }
}
void regular_command(char **command)
{
    // the parent wait
    int pid = fork();
    if (pid == 0)
    {
        handler_dfl();
        execvp(command[0], command);
        perror("ERROR - son in the background");
        exit(1);
    }
    else if (pid == -1)
    {
        // parent faild
        perror("ERROR - parent faild");
        exit(1);
    }
    else
    {
        int status;
        int state = waitpid(pid, &status, 0);
        if (state == -1 && errno != ECHILD && errno != EINTR)
        {
            perror("ERROR - wait");
            exit(1);
        }
    }
}
void pipe_process(char **command1, char **command2)
{
    int pfds[2];
    if (pipe(pfds) == -1)
    {
        close(pfds[1]);
        close(pfds[0]);
        perror("ERROR - pipe");
        exit(1);
    }
    int pid_child1 = fork();
    if (pid_child1 == -1)
    {
        perror("ERROR - fork1");
        exit(1);
    }
    else if (pid_child1 == 0)
    { //son1
        handler_dfl();
        if (dup2(pfds[1], 1) == -1)
        {
            perror("ERROR - dup2");
            exit(1);
        }
        close(pfds[1]);
        close(pfds[0]);
        execvp(command1[0], command1);
        perror("ERROR - pipe first child");
        exit(1);
    }
    else
    {
        //parent
        int pid_child2 = fork();
        if (pid_child2 == -1)
        {
            close(pfds[1]);
            close(pfds[0]);

            perror("ERROR - fork2");
            exit(1);
        }
        else if (pid_child2 == 0)
        { //son2
            handler_dfl();
            if (dup2(pfds[0], 0) == -1)
            {
                perror("ERROR - dup2");
                exit(1);
            }
            close(pfds[1]);
            close(pfds[0]);
            execvp(command2[0], command2);
            perror("ERROR - pipe second child");
            exit(1);
        }
        else
        {
            //parent
            close(pfds[1]);
            close(pfds[0]);
            int status1;
            int status2;
            int state1 = waitpid(pid_child1, &status1, 0);
            if (state1 == -1 && errno != ECHILD && errno != EINTR)
            {
                perror("ERROR - wait1");
                exit(1);
            }
            int state2 = waitpid(pid_child2, &status2, 0);
            if (state2 == -1 && errno != ECHILD && errno != EINTR)
            {
                perror("ERROR - wait1");
                exit(1);
            }
        }
    }
}
void file_process(char **command, char *file_name)
{
    int file_output = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0777);

    if (file_output < 0)
    {
        perror(" ERROR - file opening");
        exit(1);
    }
    int file = fork();
    if (file == -1)
    {
        perror("ERROR - fork");
        exit(1);
    }
    else if (file == 0)
    {
        handler_dfl();
        if (dup2(file_output, 1) == -1)
        {
            perror("ERROR - dup file");
            exit(1);
        }
        execvp(command[0], command);
        perror("ERROR - file child");
        exit(1);
    }
    else
    {
        int status;
        int state = waitpid(file, &status, 0);
        if (state == -1 && errno != ECHILD && errno != EINTR)
        {
            perror("ERROR - wait1");
            exit(1);
        }
    }
}
int process_arglist(int count, char **arglist)
{
    if (strcmp(arglist[count - 1], "&") == 0)
    {
        // background child
        arglist[count - 1] = NULL;
        background_process(arglist);
        return 1;
    }
    else
    {
        for (int i = 0; i < count; i++)
        {
            if (strcmp(arglist[i], "|") == 0)
            {
                //2 childs processes with the output of the first process
                arglist[i] = NULL;
                char **arglist2 = arglist + i + 1;
                pipe_process(arglist, arglist2);
                return 1;
            }
            if (strcmp(arglist[i], ">") == 0)
            {
                //run the child process with the output redirected to the output file
                arglist[i] = NULL;
                char *output = arglist[i + 1]; //the output file
                file_process(arglist, output);
                return 1;
            }
        }
        regular_command(arglist);
        return 1;
    }
}

int finalize(void)
{
    return 0;
}