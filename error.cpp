#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

static const int MAXLINE = 4096;

static void err_doit(int errnoflag, int error ,const char *fmt, va_list ap)
{
    char buf[MAXLINE];
    vsnprintf(buf , MAXLINE - 1 , fmt ,ap);
    if(errnoflag)
    {
        snprintf(buf + strlen(buf) , MAXLINE - strlen(buf) - 1 , ":%s",strerror(error));
    }
    strcat(buf,"\n");
    fflush(stdout);
    fputs(buf,stderr);
    fflush(NULL);
}

void err_msg(const char * fmt,...)
{
    va_list ap;
    va_start(ap,fmt);
    err_doit(1,errno,fmt,ap);
    va_end(ap);
}

void err_quit(const char * fmt,...)
{
    va_list ap;
    va_start(ap,fmt);
    err_doit(1,errno,fmt ,ap);
    va_end(ap);
    exit(1);
}