/*
 * TODO: Include your name and Andrew ID here.
 */

/*
 * TODO: Delete this comment and replace it with your own.
 * tsh - A tiny shell program with job control
 * <The line above is not a sufficient documentation.
 *  You will need to write your program documentation.
 *  Follow the 15-213/18-213/15-513 style guide at
 *  http://www.cs.cmu.edu/~213/codeStyle.html.>
 */

#include "csapp.h"
#include "tsh_helper.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * If DEBUG is defined, enable contracts and sio_printing on dbg_sio_printf.
 */
#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_sio_printf(...) sio_printf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated for these */
#define dbg_sio_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

// Struct used to store jobs
struct job_t {
    pid_t pid; // Job PID
    jid_t jid; // Job ID [1, 2, ...] defined in tsh_helper.c
    job_state state; // UNDEF, BG, FG, or ST
    char* cmdline; // Command line
};

volatile sig_atomic_t fg_child_stop;
volatile sig_atomic_t dupstd_in = -1;
volatile sig_atomic_t dupstd_out = -1;

/* Function prototypes */
void eval(const char* cmdline);
bool redirect(struct cmdline_tokens token,
    int* fd_in,
    int* fd_out,
    int* new_std_in,
    int* new_std_out);
void redirect_back(struct cmdline_tokens token,
    int* fd_in,
    int* fd_out,
    int* new_std_in,
    int* new_std_out);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);
void cleanup(void);
void non_builtin_exec(int bg, struct cmdline_tokens token, const char* cmdline);
int builtin_command(char** argv, struct cmdline_tokens token);

/*
 * TODO: Delete this comment and replace it with your own.
 * <Write main's function header documentation. What does main do?>
 * "Each function should be prefaced with a comment describing the purpose
 *  of the function (in a sentence or two), the function's arguments and
 *  return value, any error cases that are relevant to the caller,
 *  any pertinent side effects, and any assumptions that the function makes."
 */
int main(int argc, char** argv)
{
    char c;
    char cmdline[MAXLINE_TSH]; // Cmdline for fgets
    bool emit_prompt = true; // Emit prompt (default)

    // Redirect stderr to stdout (so that driver will get all output
    // on the pipe connected to stdout)
    if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
        perror("dup2 error");
        exit(1);
    }

    // Parse the command line
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h': // sio_prints help message
            usage();
            break;
        case 'v': // Emits additional diagnostic info
            verbose = true;
            break;
        case 'p': // Disables prompt sio_printing
            emit_prompt = false;
            break;
        default:
            usage();
        }
    }

    // Create environment variable
    if (putenv("MY_ENV=42") < 0) {
        perror("putenv");
        exit(1);
    }

    // Set buffering mode of stdout to line buffering.
    // This prevents lines from being sio_printed in the wrong order.
    if (setvbuf(stdout, NULL, _IOLBF, 0) < 0) {
        perror("setvbuf");
        exit(1);
    }

    // Initialize the job list
    init_job_list();

    // Register a function to clean up the job list on program termination.
    // The function may not run in the case of abnormal termination (e.g. when
    // using exit or terminating due to a signal handler), so in those cases,
    // we trust that the OS will clean up any remaining resources.
    if (atexit(cleanup) < 0) {
        perror("atexit");
        exit(1);
    }

    // Install the signal handlers
    Signal(SIGINT, sigint_handler); // Handles Ctrl-C
    Signal(SIGTSTP, sigtstp_handler); // Handles Ctrl-Z
    Signal(SIGCHLD, sigchld_handler); // Handles terminated or stopped child

    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    Signal(SIGQUIT, sigquit_handler);
    // Execute the shell's read/eval loop
    while (true) {
        if (emit_prompt) {
            sio_printf("%s", prompt);

            // We must flush stdout since we are not sio_printing a full line.
            fflush(stdout);
        }

        if ((fgets(cmdline, MAXLINE_TSH, stdin) == NULL) && ferror(stdin)) {
            perror("fgets error");
            exit(1);
        }

        if (feof(stdin)) {
            // End of file (Ctrl-D)
            sio_printf("\n");
            return 0;
        }

        // Remove any trailing newline
        char* newline = strchr(cmdline, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }

        // Evaluate the command line
        eval(cmdline);
    }

    return -1; // control never reaches here
}

/*
 * parse the command line and see if it run in background and foreground
 * cmdline: command line
 */
void eval(const char* cmdline)
{
    parseline_return parse_result;
    struct cmdline_tokens token;

    // Parse command line
    parse_result = parseline(cmdline, &token);
    // printf("parse result: %d", parse_result);

    if (parse_result == PARSELINE_ERROR || parse_result == PARSELINE_EMPTY) {
        return;
    }

    int bg; // should be the job run in bg or fg?

    if (parse_result == PARSELINE_BG) {
        bg = 1;
    } else {
        bg = 0;
    }
    if (token.argv[0] == NULL)
        return;

    if (!builtin_command(token.argv, token)) {
        non_builtin_exec(bg, token, cmdline);
    }
}

/*
 * Execute the non-builtin command line
 * bg: whether process should run in bg
 * token: command line token
 * cmdline: command line
 */
void non_builtin_exec(int bg, struct cmdline_tokens token, const char* cmdline)
{
    pid_t pid;
    sigset_t mask_all, mask_one, prev_one;

    sigfillset(&mask_all);
    sigemptyset(&mask_one);
    sigaddset(&mask_one, SIGCHLD);
    sigaddset(&mask_one, SIGINT);
    sigaddset(&mask_one, SIGTSTP);

    sigprocmask(SIG_BLOCK, &mask_one, &prev_one); //Block SIGCHLD, SIGINT, SIGTSTP
    if ((pid = fork()) == 0) {
        // sigprocmask(SIG_SETMASK, &prev_one,
        //             NULL); /*子进程中不需要堵住它，但父进程需要*/
        sigprocmask(SIG_UNBLOCK, &mask_one, NULL);
        setpgid(0, 0);
        int fin, fout, newstd_in, newstd_out;
        if (!redirect(token, &fin, &fout, &newstd_in, &newstd_out)) {
            exit(1);
        }
        if (execve(token.argv[0], token.argv, environ) < 0) {
            sio_printf("failed to execute: %s\n", token.argv[0]);
            exit(1);
        }
        exit(0);
    }

    if (!add_job(pid, bg ? BG : FG, cmdline)) {
        cleanup();
        return;
    }

    if (!bg) { // foreground
        // Parent Waits for the foreground job to terminate
        sigemptyset(&prev_one);
        fg_child_stop = 0;
        while (fg_job()) {
            //use prev_one to replace current block set
            sigsuspend(&prev_one);
        }
    } else {
        sio_printf("[%d] (%d) %s\n", job_from_pid(pid), pid, cmdline);
    }
    sigprocmask(SIG_SETMASK, &prev_one, NULL);
}

/*
 * if the command include bg or fg, run the process in bg or fg
 * argv: record command line
 */
void do_bgfg(char** argv)
{
    sigset_t prev_mask, mask_all; // to block signals
    // block signal
    sigemptyset(&mask_all);
    sigaddset(&mask_all, SIGCHLD);
    sigaddset(&mask_all, SIGINT);
    sigaddset(&mask_all, SIGTSTP);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_mask);

    if (argv[1] == NULL) {
        sio_printf("%s command requires PID or %%jobid argument\n", argv[0]);
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        return;
    }
    jid_t jid;
    pid_t pid;
    /*解析pid*/
    if (sscanf(argv[1], "%d", &pid) > 0) {
        jid = job_from_pid(pid);
        if (!jid) {
            sio_printf("%s: No such job\n", argv[1]);
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);
            return;
        }
    } else if (sscanf(argv[1], "%%%d", &jid) > 0) {
        if (!job_exists(jid)) {
            sio_printf("%s: No such job\n", argv[1]);
            sigprocmask(SIG_SETMASK, &prev_mask, NULL);
            return;
        }
        pid = job_get_pid(jid);
    } else {
        // No jid/pid
        sio_printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        return;
    }

    /*发送信号，这里要求发送到进程组，所以采用负数*/
    /*子进程的进程组id和pid是一致的，请不要将jid和进程组id搞混了*/
    if (strcmp(argv[0], "bg") == 0) {
        sio_dprintf(STDIN_FILENO, "[%d] (%d) %s\n", jid, pid,
            job_get_cmdline(jid));
        job_set_state(jid, BG);
        kill(-pid, SIGCONT);
    } else {
        job_set_state(jid, FG);
        kill(-pid, SIGCONT);

        sigemptyset(&prev_mask);
        while (fg_job()) {
            sigsuspend(&prev_mask);
        }
    }
    sigprocmask(SIG_UNBLOCK, &mask_all, NULL);
}

/**
 * redirect - redirect stdin, stdout
 */
bool redirect(struct cmdline_tokens token,
    int* fd_in,
    int* fd_out,
    int* new_std_in,
    int* new_std_out)
{
    *fd_in = -1;
    *fd_out = -1;
    *new_std_in = -1;
    *new_std_out = -1;
    if (token.infile) {
        if ((*fd_in = open(token.infile, O_RDONLY)) < 0) {
            sio_eprintf("%s: %s\n", token.infile, strerror(errno));
            return false;
        }
        dupstd_in = dup(STDIN_FILENO);
        *new_std_in = dup2(*fd_in, STDIN_FILENO); //dup fd_in to STDIN_FILENO
    }
    if (token.outfile) {
        // User, group member can rwx, others can only read
        if ((*fd_out = open(token.outfile, O_WRONLY | O_CREAT | O_TRUNC,
                 S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH))
            < 0) {
            sio_eprintf("%s: %s\n", token.outfile, strerror(errno));
            return false;
        }
        // Backup the current STDOUT
        dupstd_out = dup(STDOUT_FILENO);
        // Redirection
        *new_std_out = dup2(*fd_out, STDOUT_FILENO);
    }
    return true;
}

/**
 * redirect_back - recover stdin, stdout fileno
 */
void redirect_back(struct cmdline_tokens token,
    int* fd_in,
    int* fd_out,
    int* new_std_in,
    int* new_std_out)
{
    // Recover std input/output; close the files

    int recov = 0;
    if (token.infile) {
        recov = dup2(dupstd_in, *new_std_in);
        close(*fd_in);
        if (recov < 0) {
            return;
        }
    }
    if (token.outfile) {
        recov = dup2(dupstd_out, *new_std_out);
        close(*fd_out);
        if (recov < 0) {
            return;
        }
    }
}

/**
 * To see whether the command is the builtin command
 * argv: command line
 * token: command line token
 */
int builtin_command(char** argv, struct cmdline_tokens token)
{
    sigset_t mask_all, mask_prev;
    sigfillset(&mask_all);

    if (token.builtin == BUILTIN_QUIT)
        exit(0);
    else if (token.builtin == BUILTIN_BG)
        do_bgfg(argv);
    else if (token.builtin == BUILTIN_FG)
        do_bgfg(argv);
    else if (token.builtin == BUILTIN_JOBS) {
        sigprocmask(SIG_BLOCK, &mask_all, &mask_prev);
        int fin, fout, newstd_in, newstd_out;
        redirect(token, &fin, &fout, &newstd_in, &newstd_out);
        list_jobs(1);
        redirect_back(token, &fin, &fout, &newstd_in, &newstd_out);
        sigprocmask(SIG_SETMASK, &mask_prev, NULL);
    } else
        return 0;
    return 1;
}

/*****************
 * Signal handlers
 *****************/

/*
 * handle signal sigchld
 */
void sigchld_handler(int sig)
{
    int olderrno = errno;
    int status;
    pid_t pid;
    sigset_t mask, prev;
    sigfillset(&mask);

    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        sigprocmask(SIG_BLOCK, &mask, &prev);
        jid_t jid = job_from_pid(pid);
        if (jid == fg_job()) {
            fg_child_stop = 1;
        }
        if (WIFEXITED(status)) {
            delete_job(jid);
        } else if (WIFSIGNALED(status)) {
            sio_printf("Job [%d] (%d) terminated by signal %d\n", jid, pid,
                WTERMSIG(status));
            delete_job(jid);
        } else if (WIFSTOPPED(status)) {
            sio_printf("Job [%d] (%d) stopped by signal %d\n", jid, pid,
                WSTOPSIG(status));
            job_set_state(jid, ST);
        }
        sigprocmask(SIG_SETMASK, &prev, NULL);
    }
    errno = olderrno;
    return;
}

/*
 * handle signal sigint
 */
void sigint_handler(int sig)
{
    int oldererrno = errno;

    // block signal
    sigset_t mask_all, prev_mask;
    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_mask);

    jid_t jid = fg_job();
    if (jid <= 0) {
        // unblock
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        errno = oldererrno;
        return;
    }
    pid_t pid = job_get_pid(jid);
    kill(-pid, sig);

    // unblock
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    errno = oldererrno;
}

/*
 * handle signal sigtstp
 */
void sigtstp_handler(int sig)
{
    int oldererrno = errno;

    // block signal
    sigset_t mask_all, prev_mask;
    sigfillset(&mask_all);
    sigprocmask(SIG_BLOCK, &mask_all, &prev_mask);

    jid_t jid = fg_job();
    if (jid <= 0) {
        // unblock
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
        errno = oldererrno;
        return;
    }
    pid_t pid = job_get_pid(jid);
    kill(-pid, sig);

    // unblock
    sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    errno = oldererrno;
}

/*
 * cleanup - Attempt to clean up global resources when the program exits. In
 * particular, the job list must be freed at this time, since it may contain
 * leftover buffers from existing or even deleted jobs.
 */
void cleanup(void)
{
    // Signals handlers need to be removed before destroying the joblist
    Signal(SIGINT, SIG_DFL); // Handles Ctrl-C
    Signal(SIGTSTP, SIG_DFL); // Handles Ctrl-Z
    Signal(SIGCHLD, SIG_DFL); // Handles terminated or stopped child

    destroy_job_list();
}
