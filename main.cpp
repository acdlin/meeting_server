#include <iostream>
#include "error.h"
#include "net.h"
#include "unpthread.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>

int listenfd,room_num,thread_num;
pthread_t* tptr;
Manager* manager = nullptr;
socklen_t addrlen = 0;

void sig_chld(int signo);
int process_make(int room_no , int listenfd);
void thread_make(int num);

int main(int argc , char** argv)
{
    int room_num = 0;
    int thread_num = 0;
    int maxfd = -1;
    fd_set rset , masterset;
    FD_ZERO(&masterset);
    uint16_t port = 0;
    if(argc != 4)
    {
        std::cout <<"Usage: ./server  [port] [processes] [threads]\n";
        exit(0);
    }
    if(parse_port(argv[1] , &port) != 0)            err_quit("parse port");
    if(parse_process(argv[2] , &room_num) != 0)     err_quit("parse processes");
    if(parse_process(argv[3] , &thread_num) != 0)   err_quit("parse threads");
    tptr = new pthread_t[thread_num];
    std::vector<int> pipefds(room_num);
    manager = new Manager(room_num);
    listenfd = make_listen_socket(port , &addrlen);
    maxfd = listenfd;
    for (int time = 0 ; time < room_num ; time ++)
    {
        pipefds[time] = process_make(time, listenfd);
        FD_SET(pipefds[time] , &masterset);
        maxfd = std::max(maxfd, pipefds[time]);
    }

    for(int i = 0 ; i < thread_num ; i++)
    {
        thread_make(i);
    }

    while(true)
    {
        rset = masterset;

        int ret = select(maxfd + 1 , &rset , nullptr , nullptr , nullptr);
        if(ret == 0) continue;

        for(int i =0 ; i < room_num ; i++)
        {
            if(FD_ISSET(pipefds[i] , &rset))
            {
                char rc;
                int n;
                if((n = readn(pipefds[i] , &rc , 1)) <= 0)
                {
                    err_quit("child %d terminated unexceptedly" , i);
                }
                std::cout << "c = " << rc << std::endl;
                if(rc == 'E')
                {
                    manager->release_room_by_index(i);
                }
                else if(rc == 'Q')
                {
                    manager->remove_member_by_index(i);
                }
                else 
                {
                    err_msg("read from %d error" , pipefds[i]);
                    continue;
                }
                if(--ret == 0) break;
            }
        }
    }
    return 0;
}


int process_make(int room_index , int listenfd)
{
    void process_main(int room_no , int sockfd);
    int sock[2];
    pid_t pid;
    Socketpair(AF_LOCAL , SOCK_STREAM , 0 ,sock);
    if((pid = fork()) > 0)
    {
        close(sock[1]);
        manager->setinfo(room_index , pid , sock[0]);
        return sock[0];
    }
    else if (pid == 0)
    {
        close(listenfd);
        close(sock[0]);
        process_main(room_index , sock[1]);
        exit(0);
    }
    else
    {
        err_quit("fork");
    }
    return -1;
}

void thread_make(int num)
{
    void* thread_main(void*);
    int* arg = new int(num);
    Pthread_create(&tptr[num] , nullptr , thread_main, arg);
}