#include <pthread.h>
#include "error.h"
#include <errno.h>

void Pthread_create(pthread_t* tid , pthread_attr_t *attr , void *(*func)(void *) , void * arg)
{
    int n;
    if((n = pthread_create(tid , attr , func , arg)) != 0)
    {
        errno = n;
        err_quit("pthread create");
    }
}

void Pthread_detach(pthread_t tid)
{
    int n;
    if((n = pthread_detach(tid)) == 0)
    {
        return;
    }
    else
    {
        errno = n;
        err_quit("pthread detack error");
    }
}

void Pthread_mutex_lock(pthread_mutex_t* mtx)
{
    int n;
    if((n = pthread_mutex_lock(mtx))  == 0)
    {
        return;
    } 
    else
    {
        errno = n;
        err_quit("pthread_mutex_lock error");
    }
}

void Pthread_mutex_unlock(pthread_mutex_t* mtx)
{
    int n;
    if((n = pthread_mutex_unlock(mtx))  == 0)
    {
        return;
    }
    else
    {
        errno = n;
        err_quit("pthread_mutex_unlock error");
    }
}

void Pthread_cond_signal(pthread_cond_t *cond)
{
    int n;
    if((n = pthread_cond_signal(cond)) == 0)
    {
        return;
    }
    else
    {
        errno = n;
        err_quit("Pthread_cond_signal error");
    }
}

void Pthread_cond_wait(pthread_cond_t* cond , pthread_mutex_t* mtx)
{
    int n;
    if((n = pthread_cond_wait(cond , mtx)) == 0)
    {
        return ;
    }
    else
    {
        errno = n;
        err_quit("Pthread_cond_wait error");
    }
}
