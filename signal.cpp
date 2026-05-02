#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include "signal_util.h"
#include "error.h"


void sig_chld(int signo)
{
    int stat;
    while(waitpid(-1 , &stat , WNOHANG) > 0)
    {
    }
    return;
}

Sigfunc* Signal(int signo , Sigfunc* func)
{
    struct sigaction act{} , old{};
    act.sa_handler = func;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if(signo == SIGALRM){
#ifdef SA_INTERRUPT 
        act.sa_flags |= SA_INTERRUPT;
#endif
    }
    else{
#ifdef SA_RESTART
        act.sa_flags |= SA_RESTART;
#endif 
    }
    if(sigaction(signo , &act , &old) < 0)
    {
        err_msg("sigaction");
        return SIG_ERR;
    }
    return old.sa_handler;
}
