#include "error.h"
#include <iostream>
#include <sys/types.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

int make_listen_socket(uint16_t port)
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

    return fd;

}