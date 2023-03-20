#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"
#include "swish_funcs.h"

#define MAX_ARGS 10

int tokenize(char *s, strvec_t *tokens) {
    // TODO Task 0: Tokenize string s
    // Assume each token is separated by a single space (" ")
    // Use the strtok() function to accomplish this
    // Add each token to the 'tokens' parameter (a string vector)
    // Return 0 on success, -1 on error

    if (s == NULL || tokens == NULL) {
        return -1; 
    }
    
    char *token = strtok(s, " ");
    while (token != NULL) {
	strvec_add(tokens,token);
        token = strtok(NULL, " ");
    }
    
    return 0; 
}

int run_command(strvec_t *tokens) {
    // TODO Task 2: Execute the specified program (token 0) with the
    // specified command-line arguments
    // THIS FUNCTION SHOULD BE CALLED FROM A CHILD OF THE MAIN SHELL PROCESS
    // Hint: Build a string array from the 'tokens' vector and pass this into execvp()
    // Another Hint: You have a guarantee of the longest possible needed array, so you
    // won't have to use malloc.
	
	char *args[tokens->length + 1];
    for (int i = 0; i < tokens->length; i++) {
        args[i] = strvec_get(tokens, i);
    }
    args[tokens->length] = NULL;
    

    execvp(args[0], args);
    

    perror("execvp");
    
    // TODO Task 3: Extend this function to perform output redirection before exec()'ing
    // Check for '<' (redirect input), '>' (redirect output), '>>' (redirect and append output)
    // entries inside of 'tokens' (the strvec_find() function will do this for you)
    // Open the necessary file for reading (<), writing (>), or appending (>>)
    // Use dup2() to redirect stdin (<), stdout (> or >>)
    // DO NOT pass redirection operators and file names to exec()'d program
    // E.g., "ls -l > out.txt" should be exec()'d with strings "ls", "-l", NULL

    int input_fd = -1;
    int output_fd = -1;


    int input_index = strvec_find(tokens, "<");
    int output_index = strvec_find(tokens, ">");
    int append_index = strvec_find(tokens, ">>");


    if (input_index >= 0) {
        char *input_file = strvec_get(tokens, input_index + 1);
        input_fd = open(input_file, O_RDONLY);
        if (input_fd == -1) {
            perror("unable to open file");
            return -1;
        }
        // Redirect stdin to the input file
        if (dup2(input_fd, STDIN_FILENO) == -1) {
            perror("dup2 error");
            return -1;
        }
    }


    if (output_index >= 0) {
        char *output_file = strvec_get(tokens, output_index + 1);
        output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (output_fd == -1) {
            perror("unable to open file");
            return -1;
        }

        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2 error");
            return -1;
        }
    }


    if (append_index >= 0) {
        char *append_file = strvec_get(tokens, append_index + 1);
        output_fd = open(append_file, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (output_fd == -1) {
            perror("error opening file");
            return -1;
        }

        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2 error");
            return -1;
        }
    }
    // TODO Task 4: You need to do two items of setup before exec()'ing
    // 1. Restore the signal handlers for SIGTTOU and SIGTTIN to their defaults.
    // The code in main() within swish.c sets these handlers to the SIG_IGN value.
    // Adapt this code to use sigaction() to set the handlers to the SIG_DFL value.
    // 2. Change the process group of this process (a child of the main shell).
    // Call getpid() to get its process ID then call setpgid() and use this process
    // ID as the value for the new process group ID
    struct sigaction sigtstp_action, sigttin_action, sigttou_action;
    sigtstp_action.sa_handler = SIG_DFL;
    sigemptyset(&sigtstp_action.sa_mask);
    sigtstp_action.sa_flags = 0;

    sigttin_action.sa_handler = SIG_DFL;
    sigemptyset(&sigttin_action.sa_mask);
    sigttin_action.sa_flags = 0;

    sigttou_action.sa_handler = SIG_DFL;
    sigemptyset(&sigttou_action.sa_mask);
    sigttou_action.sa_flags = 0;


    sigaction(SIGTSTP, &sigtstp_action, NULL);
    sigaction(SIGTTIN, &sigttin_action, NULL);
    sigaction(SIGTTOU, &sigttou_action, NULL);


    pid_t pid = getpid();
    if (setpgid(pid, pid) < 0) {
        perror("setpgid");
        exit(1);
    }
    // Not reachable after a successful exec(), but retain here to keep compiler happy
    return 0;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
int job_id;
    if (tokens->length < 2 || sscanf(tokens->data[1], "%d", &job_id) != 1) {
        fprintf(stderr, "usage: resume <jobid>\n");
        return 1;
    }

    job_t *job = job_list_get(jobs, job_id);
    if (!job) {
        fprintf(stderr, "job with id %d not found\n", job_id);
        return 1;
    }

    
    if (job->status == JOB_STOPPED) {
        job->status = JOB_BACKGROUND;
    }

   
    if (kill(-job->pid, SIGCONT) < 0) {
        perror("kill (SIGCONT)");
        return 1;
    }

   
    if (is_foreground) {
        if (tcsetpgrp(STDIN_FILENO, job->pid) < 0) {
            perror("tcsetpgrp");
            return 1;
        }
       
        await_background_job(tokens, jobs);
      
        if (tcsetpgrp(STDIN_FILENO, getpid()) < 0) {
            perror("tcsetpgrp");
            return 1;
        }
    }

    return 0;
    // TODO Task 5: Implement the ability to resume stopped jobs in the foreground
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    //    Feel free to use sscanf() or atoi() to convert this string to an int
    // 2. Call tcsetpgrp(STDIN_FILENO, <job_pid>) where job_pid is the job's process ID
    // 3. Send the process the SIGCONT signal with the kill() system call
    // 4. Use the same waitpid() logic as in main -- dont' forget WUNTRACED
    // 5. If the job has terminated (not stopped), remove it from the 'jobs' list
    // 6. Call tcsetpgrp(STDIN_FILENO, <shell_pid>). shell_pid is the *current*
    //    process's pid, since we call this function from the main shell process

    // TODO Task 6: Implement the ability to resume stopped jobs in the background.
    // This really just means omitting some of the steps used to resume a job in the foreground:
    // 1. DO NOT call tcsetpgrp() to manipulate foreground/background terminal process group
    // 2. DO NOT call waitpid() to wait on the job
    // 3. Make sure to modify the 'status' field of the relevant job list entry to JOB_BACKGROUND
    //    (as it was JOB_STOPPED before this)
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    // TODO Task 6: Wait for a specific job to stop or terminate
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    // 2. Make sure the job's status is JOB_BACKGROUND (no sense waiting for a stopped job)
    // 3. Use waitpid() to wait for the job to terminate, as you have in resume_job() and main().
    // 4. If the process terminates (is not stopped by a signal) remove it from the jobs list


    int job_idx;
    if (tokens->length < 2 || sscanf(tokens->data[1], "%d", &job_idx) != 1) {
        printf("Usage: bg <job>\n");
        return 1;
    }

    job_t *job = job_list_get(jobs, job_idx);
    if (!job) {
        printf("Job %d not found\n", job_idx);
        return 1;
    }

    if (job->status != JOB_BACKGROUND) {
        printf("Job %d is not a background job\n", job_idx);
        return 1;
    }

  
    int status;
    pid_t pid = waitpid(job->pid, &status, WUNTRACED);


    if (pid > 0) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            job_list_remove(jobs, job_idx);
        }
    }
    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    // Iterate through the jobs list, ignoring any stopped jobs
    job_t *job = jobs->head;
    while (job) {
        if (job->status == JOB_STOPPED) {
            job = job->next;
            continue;
        }

        // Call waitpid() with WUNTRACED for background jobs
        if (job->status == JOB_BACKGROUND) {
            int status;
            pid_t pid = waitpid(job->pid, &status, WUNTRACED);

            if (WIFSTOPPED(status)) {
                // If the job has stopped, change its status to JOB_STOPPED
                job->status = JOB_STOPPED;
            } else if (pid > 0) {
                // If the job has terminated, remove it from the list
                job_list_remove_by_status(jobs, job->status);
            }
        }

        job = job->next;
    }

    // Remove all background jobs from the jobs list
    job_list_remove_by_status(jobs, JOB_BACKGROUND);

    return 0;
}
