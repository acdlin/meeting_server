#include "error.h"
#include "server.h"
#include "net.h"
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <algorithm>

static void broadcast_room(int room_id , const std::string& msg , int max_fd , fd_set* master_readfds , int fd_room[])
{
    for(int i = 0 ; i <= max_fd ; i++)
    {
        if(!FD_ISSET(i , master_readfds)) continue;
        if(fd_room[i] == room_id) send_text(i , msg);
    }
}

static void close_room(int room_id ,const std::string& msg , int max_fd , fd_set* master_readfds ,int fd_room[], int room_own[])
{
    for(int i = 0 ; i <= max_fd ; i++)
    {
        if(!FD_ISSET(i , master_readfds)) continue;
        if(fd_room[i] == room_id) 
        {
            send_text( i , msg);
            fd_room[i] = -1;
        }
    }
    room_own[room_id] = -1;
}

int main(int argc, char ** argv)
{

    if(argc != 2){
        std::cerr << "Usage: " << argv[0] << " port\n";
        return 2;
    }
    uint16_t port = 0;
    if(parse_port(argv[1], &port) != 0) err_quit("parse port");
    int listenfd = make_listen_socket(port);
    std::cout << "listen on 0.0.0.0:" << port << std::endl;


    fd_set master_readfds;
    int fd_room[FD_SETSIZE] {};//该fd在哪个房间
    int room_own[FD_SETSIZE] {};//这个房间的房主
    std::fill(fd_room,fd_room + FD_SETSIZE , -1);
    std::fill(room_own,room_own + FD_SETSIZE , -1);
    FD_ZERO(&master_readfds);
    FD_SET(listenfd, &master_readfds);
    int max_fd = listenfd;
    sockaddr_in peer_addrs[FD_SETSIZE]{};


    while(1)
    {
        fd_set read_fds = master_readfds;
        int nready = select(max_fd + 1, &read_fds , NULL , NULL , NULL);
        if(nready < 0)
        {
            if(errno == EINTR) continue;
            err_quit("select");
        }


        //处理新连接
        if(FD_ISSET(listenfd , &read_fds))
        {
            sockaddr_in cli{};
            socklen_t len = sizeof(cli);

            int connfd;
            while((connfd = accept(listenfd ,(sockaddr*)&cli,&len)) < 0)
            {
                if(errno == EINTR) continue;
                err_msg("accept");
                break;
            }
            if(connfd >=0)
            {
                if(connfd >= FD_SETSIZE)
                {
                    std::cerr << "too many fds .fd:" << connfd << std::endl;
                    close(connfd);
                }
                else
                {
                    FD_SET(connfd, &master_readfds);
                    if(connfd > max_fd) max_fd = connfd;
                    peer_addrs[connfd] = cli;
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET , &cli.sin_addr , ip,INET_ADDRSTRLEN);
                    std::cout <<"[+] new connfd:" << connfd << " from" << ip << ": " << ntohs(cli.sin_port) << std::endl;
                }
                
            }
            if(--nready <= 0) continue;
        }


        //开始处理返回set
        for(int fd =0 ; fd <= max_fd ; fd++)
        {
            if(fd == listenfd) continue;
            if(!FD_ISSET( fd , &read_fds)) continue;
            Frame fr;
            int r = read_frame(fd , fr);
            if(r == 1)
            {
                if(fr.type == MsgType::TEXT_SEND)
                {
                    if(fd_room[fd] == -1)
                    {
                        std::string msg = "you are not in any room .Please JOIN_MEETING first.";
                        send_text(fd , msg);
                    }
                    else
                    {   
                        int room_id = fd_room[fd];
                        char ip_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET , &peer_addrs[fd].sin_addr, ip_str , sizeof(ip_str));
                        std::string s = "[" + std::to_string(room_id) + "]" + ip_str + ":" + std::string(fr.payload.begin() , fr.payload.end()) + "\n";
                        broadcast_room(room_id , s , max_fd , &master_readfds , fd_room );
                    }
                }
                else if(fr.type == MsgType::CREATE_MEETING)
                {
                    if(fd_room[fd] != -1)
                    {
                        std::string msg = "you are already in room .";
                        send_text(fd , msg);
                    }
                    else
                    {
                        for(int i = 1; i < FD_SETSIZE ; i++)
                        {
                            if(room_own[i] == -1)
                            {
                                room_own[i] = fd;
                                fd_room[fd] = i;
                                std::string msg = "create room "+ std::to_string(i) + "\n";
                                send_text(fd , msg);
                                break;
                            }
                        }
                    }
                }
                else if(fr.type == MsgType::JOIN_MEETING)
                {
                    if(fd_room[fd] != -1)
                    {
                        std::string msg = "you are already in room" + std::to_string(fd_room[fd]) + "\n";
                        send_text(fd, msg);
                        continue;
                    }
                    if(fr.payload.size() != 4)
                    {
                        std::string msg = "there is a problem with your frame.";
                        send_text(fd , msg);
                        continue;
                    }
                        uint32_t room_net;
                        memcpy(&room_net , fr.payload.data() , 4);
                        uint32_t room_id = ntohl(room_net);
                        if(room_id <= 0 || room_id >= FD_SETSIZE)
                        {
                            std::string msg = "room id out of range.";
                            send_text(fd , msg);
                            continue;
                        }
                        if(room_own[room_id] == -1)
                        {
                            std::string msg = "room " + std::to_string(room_id) + " is not existed." + "\n";
                            send_text(fd , msg);
                            continue;
                        }
                        fd_room[fd] = room_id;
                        std::string msg = "you are in room " + std::to_string(room_id) + "\n";
                        send_text(fd , msg);
                        continue;
                }

                else if(fr.type == MsgType::EXIT_MEETING)
                {
                    if(fd_room[fd] == -1)
                    {
                        std::string msg = "you are not in any room .";
                        send_text(fd , msg);
                        continue;
                    }
                    int room_id = fd_room[fd];
                    if(room_own[room_id] == fd)
                    {
                        close_room(room_id , "room closed by owner.\n", max_fd ,&master_readfds , fd_room , room_own);
                    }
                    else
                    {
                        fd_room[fd] = -1;
                    }
                }
            }

            //断开连接
            else if(r == 0)
            {
                int room_id = fd_room[fd];
                close(fd);
                FD_CLR(fd , &master_readfds);
                if(room_id != -1)
                {
                    if(room_own[room_id] == fd)
                    {
                        close_room(room_id , "room is closed by owner.\n" , max_fd , &master_readfds , fd_room , room_own);

                    }
                    else
                    {
                        fd_room[fd] = -1;
                    }
                }

                if(fd == max_fd)
                {
                    while(max_fd >= 0 && !FD_ISSET(max_fd , &master_readfds)){
                        max_fd--;
                    }
                }

            }
            
            //异常返回
            else
            {
                if(errno == EINTR) continue;
                err_msg("recv");
                close(fd);
                FD_CLR(fd , &master_readfds);
                if(fd == max_fd)
                {
                    while(max_fd >= 0 && !FD_ISSET(max_fd,&master_readfds))
                    {
                        max_fd--;
                    }
                }

            }
            

            //已无待处理的fd
            if(--nready <= 0) break;
        }
    }
    close(listenfd);
    return 0;
}

