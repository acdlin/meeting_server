#include <iostream>
#include <vector>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "msg.h"
#include "net.h"
#include "error.h"

pthread_mutex_t t_lock = PTHREAD_MUTEX_INITIALIZER;
extern int listenfd;
extern pthread_t* tptr;
extern Manager* manager;

void* thread_main(void* arg)
{
    void dowithuser(int );
    int thread_id = *reinterpret_cast<int*>(arg);
    delete reinterpret_cast<int*>(arg);
    Pthread_detach(pthread_self());
    printf("thread %d starting...\n", thread_id );
    int connfd = -1;
    while(true)
    {
        Pthread_mutex_lock(&t_lock);
        sockaddr_storage cli_addr{};
        socklen_t len = sizeof(cli_addr);
        connfd = Accept (listenfd , reinterpret_cast<sockaddr*>(&cli_addr) , &len);
        Pthread_mutex_unlock(&t_lock);
        char addrbuf[128] = {0};
        std::cout << "new connection in " << Sock_ntop(addrbuf , 128 , reinterpret_cast<sockaddr*>(&cli_addr) , len) << std::endl;
        dowithuser(connfd);
    }
    return nullptr;
}

void dowithuser(int connfd)
{
    int writetofd(int , const MSG&);
    char head[15] = {0};

    while(true)
    {
        ssize_t ret = readn(connfd , head , 11 );
        if(ret <= 0)
        {
            close(connfd);
            std::cout << connfd << " close\n";
            return;
        }
        else if(ret <11 || head[0] != '$')
        {
            close(connfd);
            err_msg("data error");
            return ;
        }
        else
        {
            uint16_t type_net;
            memcpy(&type_net , head +1 , 2);
            MsgType msgtype = static_cast<MsgType>(ntohs(type_net));

            uint32_t ip;
            memcpy(&ip , head + 3 , 4);
            uint32_t datasize;
            memcpy(&datasize , head + 7 , 4);
            datasize = ntohl(datasize);
            if(msgtype == MsgType::CREATE_MEETING)
            {
                char tail;
                readn(connfd , &tail , 1);
                if(datasize != 0 || tail != '#')
                {
                    close(connfd);
                    err_msg("err frame");
                }
                else
                {
                    int roomNo;
                    if((roomNo = manager->create_room()) == -1)
                    {
                        roomNo = 0;
                        MSG msg{};
                        msg.type = MsgType::CREATE_MEETING_RESPONSE;
                        uint32_t room_net = htonl(static_cast<uint32_t>(roomNo));
                        msg.payload.assign(reinterpret_cast<const char*>(&room_net) , sizeof(room_net));
                        writetofd(connfd , msg);
                    }
                    else
                    {
                        RoomCmd cmd{};
                        cmd.cmd = 'C';
                        cmd.room_no_net = htonl(static_cast<uint32_t>(roomNo));
                        write_fd(manager->getroompipe(roomNo) , &cmd , sizeof(cmd) , connfd);
                        close(connfd);
                        std::cout << "room " << roomNo << " used.\n";
                        return;
                    }
                }
            }
            else if(msgtype == MsgType::JOIN_MEETING)
            {
                if (datasize != sizeof(uint32_t))
                {
                    err_msg("join payload size error");
                    close(connfd);
                    return;
                }

                uint32_t roomno_net = 0;
                char tail = 0;

                if (readn(connfd, &roomno_net, sizeof(roomno_net)) != sizeof(roomno_net) ||
                    readn(connfd, &tail, 1) != 1 ||
                    tail != '#')
                {
                    err_msg("join format error");
                    close(connfd);
                    return;
                }

                uint32_t roomno = ntohl(roomno_net);
                int ret = manager->join_room(roomno);

                MSG msg{};
                msg.type = MsgType::JOIN_MEETING_RESPONSE;

                if (ret >= 0)
                {
                    uint32_t room_net = htonl(static_cast<uint32_t>(ret));
                    msg.payload.assign(reinterpret_cast<const char*>(&room_net), sizeof(room_net));
                    writetofd(connfd, msg);

                    RoomCmd cmd{};
                    cmd.cmd = 'J';
                    cmd.room_no_net = room_net;
                    write_fd(manager->getroompipe(roomno), &cmd, sizeof(cmd), connfd);

                    close(connfd);
                    return;
                }
                uint32_t fail_net = htonl(0);
                msg.payload.assign(reinterpret_cast<const char*>(&fail_net), sizeof(fail_net));
                writetofd(connfd, msg);
                close(connfd);
                return;
            }
            else
            {
                close(connfd);
                err_msg("error format");
            }
        }
    }
}

int writetofd(int fd, const MSG& msg)
{
    std::vector<uint8_t> buf;
    buf.reserve(1 + 2 + 4 + 4 + msg.payload.size() + 1);

    buf.push_back(static_cast<uint8_t>('$'));

    uint16_t type = htons(static_cast<uint16_t>(msg.type));
    buf.insert(buf.end(),reinterpret_cast<const uint8_t*>(&type),reinterpret_cast<const uint8_t*>(&type) + sizeof(type));

    buf.insert(buf.end(),reinterpret_cast<const uint8_t*>(&msg.ip_net),reinterpret_cast<const uint8_t*>(&msg.ip_net) + sizeof(msg.ip_net));

    uint32_t len = htonl(static_cast<uint32_t>(msg.payload.size()));
    buf.insert(buf.end(),reinterpret_cast<const uint8_t*>(&len),reinterpret_cast<const uint8_t*>(&len) + sizeof(len));

    buf.insert(buf.end(),reinterpret_cast<const uint8_t*>(msg.payload.data()),reinterpret_cast<const uint8_t*>(msg.payload.data()) + msg.payload.size());

    buf.push_back(static_cast<uint8_t>('#'));

    return (writen(fd, buf.data(), buf.size()) == static_cast<ssize_t>(buf.size())) ? 0 : -1;
}
