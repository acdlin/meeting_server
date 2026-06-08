#include "net.h"
#include "error.h"
#include <iostream>
#include <cstdint>
#include <vector>
#include <unistd.h>
#include <errno.h>
#include <cstddef>
#include <arpa/inet.h>
#include <cstring>
#include <climits>
#include <fcntl.h>
#include <sys/epoll.h>

int parse_port(const char* start , uint16_t * port)
{
    char* end = nullptr;
    errno = 0;
    unsigned long val = strtoul(start , &end , 10);
    if( start == end || *end != '\0' || errno == ERANGE || val > 65535) return -1;
    *port = static_cast<uint16_t>(val);
    return 0;
}

int parse_process(const char* start , int * processes)
{
    char* end = nullptr;
    errno = 0;
    unsigned long val = strtoul(start , &end , 10);
    if( start == end || *end != '\0' || errno == ERANGE || val == 0 || val > INT_MAX) return -1;
    *processes = static_cast<int>(val);
    return 0;
}

void Socketpair(int domain , int type , int protocol , int* sock)
{
    if(socketpair(domain , type , protocol , sock) < 0)
    {
        err_quit("sockerpair");
    }
}


//读满n个字符，返回读取的字符数量
ssize_t readn(int fd , void* buf , size_t n)
{
    size_t remain = n;
    char* p = static_cast<char*>(buf);
    while(remain > 0)
    {
        ssize_t r = read(fd , p , remain);
        if(r > 0)
        {
            remain -= r;
            p += r;
        }
        else if(r == 0)
        {
            break;
        }
        else
        {
            if(errno == EINTR)  continue;
            if(errno == EAGAIN || errno == EWOULDBLOCK)    break;
            return -1;
        }
    }
    return n - remain;
}


//写满n个字符，返回写了的字符数量
ssize_t writen(int fd ,const void* buf ,size_t n)
{
    size_t remain = n;
    const char * p = static_cast< const char *>(buf);
    while(remain > 0)
    {
        ssize_t w = ::write( fd , p , remain);
        if(w < 0)
        {
            if(errno == EINTR) continue;
            if(errno == EWOULDBLOCK || errno == EAGAIN) break;
            return -1;
        } 
        if(w == 0) break;

        remain -= w;
        p += w;
    }
    return static_cast<ssize_t>(n - remain);
}



int make_listen_socket(uint16_t port , socklen_t * addrlen )
{
    int fd = 0;
    fd = socket(AF_INET ,SOCK_STREAM , 0);
    if( fd < 0 )
    {
        err_quit("socket");
        return -1;
    }
    int opt = 1;
    
    if(setsockopt(fd , SOL_SOCKET , SO_REUSEADDR , &opt , sizeof(opt)) < 0)
    {
        err_quit("setsockopt");
        close(fd);
        return -1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        err_quit("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 128) < 0)
    {
        err_quit("listen");
        close(fd);
        return -1;
    }
    if(addrlen)
    {
        *addrlen = sizeof(sockaddr_in);
    }
    return fd;

}

int Epoll_create(int flags)
{
    int epfd = 0;
    epfd = epoll_create1(flags);
    if(epfd == -1)
    {
        err_quit("epoll_create");
    }
    return epfd;
}

void write_fd(int sockfd , const void *ptr , size_t nbytes , int fdtosend)
{
    struct msghdr msg{};
    struct iovec iov{};

    iov.iov_base = const_cast<void*>(ptr);
    iov.iov_len = nbytes;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    char control[CMSG_SPACE(sizeof(int))]{};
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    *reinterpret_cast<int*>(CMSG_DATA(cmsg)) = fdtosend;

    if(sendmsg(sockfd , &msg , 0) < 0)
    {
        err_quit("write fd");
    }

}

void recv_fd(int sockfd , void* ptr ,size_t nbytes ,  int* fd)
{
    struct msghdr msg{};
    struct iovec iov{};
    iov.iov_base = ptr;
    iov.iov_len = nbytes;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    char control[CMSG_SPACE(sizeof(int))]{};
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    if(recvmsg(sockfd , &msg , 0) < 0)
    {
        err_quit("recv msg");
    }

    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if(cmsg == nullptr)
    {
        err_quit("err read msg");
    }
    if(cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
    {
        err_quit("err type");
    }
    memcpy(fd , CMSG_DATA(cmsg) , sizeof(int));

}
const char* Sock_ntop(char* str, int size, const sockaddr* sa, socklen_t salen)
{
    if (!str || size <= 0 || !sa) return nullptr;
    str[0] = '\0';

    switch (sa->sa_family)
    {
    case AF_INET:
    {
        if (salen < static_cast<socklen_t>(sizeof(sockaddr_in))) return nullptr;
        const auto* sin = reinterpret_cast<const sockaddr_in*>(sa);

        if (!inet_ntop(AF_INET, &sin->sin_addr, str, size)) return nullptr;

        const uint16_t port = ntohs(sin->sin_port);
        if (port > 0) {
            const int used = static_cast<int>(strlen(str));
            if (used < size) {
                snprintf(str + used, size - used, ":%u", port);
            }
        }
        return str;
    }

    case AF_INET6:
    {
        if (salen < static_cast<socklen_t>(sizeof(sockaddr_in6))) return nullptr;
        const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(sa);

        if (!inet_ntop(AF_INET6, &sin6->sin6_addr, str, size)) return nullptr;

        const uint16_t port = ntohs(sin6->sin6_port);
        if (port > 0) {
            const int used = static_cast<int>(strlen(str));
            if (used < size) {
                snprintf(str + used, size - used, ":%u", port);
            }
        }
        return str;
    }

    default:
        return nullptr;
    }
}
int Accept(int listenfd, sockaddr* addr, socklen_t *addrlen)  //带错误处理的accept
{
    while(true)
    {
        int n = accept(listenfd , addr , addrlen);

        if( n >= 0 )
        {
            return n;
        }
        else if(errno == EINTR)
        {
            continue;
        }
        else if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return -1;
        }
        err_quit("accept error");
    }
}

uint32_t getpeerip(int connfd)
{
    sockaddr_in peeraddr;
    socklen_t addrlen = sizeof(peeraddr);
    if(getpeername(connfd , (sockaddr*)&peeraddr , &addrlen) < 0)
    {
        err_msg("getpeername error");
        return -1;
    }
    return peeraddr.sin_addr.s_addr;
}

uint16_t getpeerport(int connfd)
{
    sockaddr_in peeraddr;
    socklen_t addrlen = sizeof(peeraddr);
    if(getpeername(connfd, (sockaddr*)&peeraddr, &addrlen) < 0)
    {
        err_msg("getpeername error");
        return 0;
    }
    return peeraddr.sin_port;
}