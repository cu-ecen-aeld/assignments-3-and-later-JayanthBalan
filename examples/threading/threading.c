#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...) syslog(LOG_DEBUG, "threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) syslog(LOG_ERR, "threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data *tf_args = (struct thread_data*)thread_param;

    usleep((tf_args->wait_to_obtain_ms)*1000);
    pthread_mutex_lock(tf_args->mutex);
    usleep(1000*(tf_args->wait_to_release_ms));
    pthread_mutex_unlock(tf_args->mutex);

    tf_args->thread_complete_success = true;
    return tf_args;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex, int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    openlog(NULL, LOG_PID, LOG_USER);
    syslog(LOG_INFO, "Service start successful.");
    struct thread_data *t_dat = (struct thread_data*)malloc(sizeof(struct thread_data));
    if(t_dat == NULL) {
        syslog(LOG_ERR, "Structure creation failed.");
        perror("Fail");
        closelog();
        return false;
    }

    t_dat->mutex = mutex;
    t_dat->wait_to_obtain_ms = wait_to_obtain_ms;
    t_dat->wait_to_release_ms = wait_to_release_ms;
    t_dat->thread_complete_success = false;

    int status = pthread_create(thread, NULL, threadfunc, (void*)t_dat);
    if(status != 0) {
        syslog(LOG_ERR, "Thread creation failed.");
        perror("Fail");
        closelog();
        return false;
    }

    closelog();
    return true;
}

