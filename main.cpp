#include "error.h"
#include "server.h"
#include "net.h"
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <fcntl.h>
#include <unordered_map>

struct Conn{
    int fd;
    sockaddr_storage addr;
    socklen_t addrlen;
    int room_id = -1;
};

struct Room{
    int owner = -1;
    std::vector<int> members;
};

static std::string peer_to_string( Conn* conn)
{
    int port = 0;
    char ip[INET6_ADDRSTRLEN] = {0};
    if(conn->addr.ss_family == AF_INET)
    {
        auto a = reinterpret_cast<sockaddr_in*>(&conn->addr);
        inet_ntop(AF_INET , &a->sin_addr , ip , sizeof(ip));
        port = ntohs(a->sin_port);
        return std::string(ip) + ":" + std::to_string(port);
    }
    else if(conn->addr.ss_family == AF_INET6)
    {
        auto a = reinterpret_cast<sockaddr_in6*>(&conn->addr);
        inet_ntop(AF_INET6 , &a->sin6_addr , ip , sizeof(ip));
        port = ntohs(a->sin6_port);
        return std::string(ip) + ":" + std::to_string(port);
    }
    else
    {
        return "unknown";
    }
}

static int handle_new_connect(int listenfd ,int epfd , std::unordered_map<int , Conn*>& fd_conn_map)
{
    Conn* conn = new Conn{};
    conn->addrlen = sizeof(conn->addr);
    int connfd = accept(listenfd , reinterpret_cast<sockaddr*>(&conn->addr) ,&conn->addrlen);
    if(connfd < 0)
    {
        delete(conn);
        return -1;
    }
    conn->fd = connfd;
    int flags = fcntl(connfd , F_GETFL , 0);
    if (flags == -1) 
    {
        close(connfd);
        delete conn;
        return -1;
    }

    if(fcntl(connfd , F_SETFL , flags|O_NONBLOCK) == -1)
    {
        delete(conn);
        close(connfd);
        return -1;
    }
    epoll_event conn_ev{};
    conn_ev.data.fd = connfd;
    conn_ev.events = EPOLLIN|EPOLLET;
    if(epoll_ctl(epfd , EPOLL_CTL_ADD , connfd , &conn_ev) == -1)
    {
        delete(conn);
        close(connfd);
        return -1;
    }
    
    fd_conn_map.insert({connfd , conn});
    std::string net_addr = peer_to_string(conn);
    std::cout << "[+]new connection :" << net_addr << std::endl;
    return 0;
}

static void handle_join_meeting(Conn* conn , std::vector<Room>& room_ptr , std::unordered_map<int , Conn*>& fd_conn_map , int target_room_id)
{
    if(conn->room_id != -1)
    {
        send_string_frame(conn->fd , MsgType::ERROR_MSG , "you are already in room "+ std::to_string(conn->room_id) + "\n");    
    }
    else if(room_ptr[target_room_id].owner == -1)
    {
        send_string_frame(conn->fd , MsgType::ERROR_MSG , "room "+ std::to_string(target_room_id) + " is closed.\n");  
    }
    else
    {
        conn->room_id = target_room_id;
        room_ptr[conn->room_id].members.push_back(conn->fd);
        for(auto room_members : room_ptr[conn->room_id].members)
        {
            if(room_members != conn->fd)
            {
                send_string_frame(room_members , MsgType::PARTNER_JOIN ,  std::to_string(conn->fd) + " is comming.\n");
            }
        }
        send_string_frame(conn->fd , MsgType::TEXT_RECV , "you are in room " + std::to_string(target_room_id) + "\n");
    }
}
static int handle_create_meeting(int fd , Conn* conn  , std::vector<Room>& room_ptr)
{
    if(conn->room_id != -1)
    {
        send_string_frame(conn->fd , MsgType::ERROR_MSG , "you are already in room "+ std::to_string(conn->room_id) + "\n");
        return -1;
    }
    int room_id = -1;
    for (std::size_t i = 1; i < room_ptr.size(); ++i)
    {
        if(room_ptr[i].owner == -1)
        {
            room_id = i;
            room_ptr[i].owner = fd;
            break;
        }
    }
    if(room_id == -1)
    {
        return -1;
    }
    room_ptr[room_id].owner = fd;
    room_ptr[room_id].members.push_back(fd);
    conn->room_id = room_id;
    if(send_u32_frame(fd , MsgType::CREATE_MEETING_RESPONSE , static_cast<uint32_t>(room_id)) != 0)
    {
        return -1;
    }
    return 0;
}

static void handle_close(Conn* conn ,int epfd  , std::vector<Room> & room_ptr , std::unordered_map<int , Conn*>& fd_conn_map ,int* available = nullptr)
{
    int room_id = conn->room_id;
    if(room_id <= 0)
    {
        epoll_ctl(epfd , EPOLL_CTL_DEL ,conn->fd , nullptr);
        fd_conn_map.erase(conn->fd);
        close(conn->fd);
        delete(conn);
        return;
    }

    if(conn->fd == room_ptr[room_id].owner)
    {
        for(auto member_fd : room_ptr[room_id].members)
        {
            if(member_fd != conn->fd)
            {
                send_string_frame(member_fd , MsgType::ROOM_CLOSED , "owner exit " + peer_to_string(conn));
                fd_conn_map[member_fd]->room_id = -1;
            }
        }
        room_ptr[room_id].members.clear();
        room_ptr[room_id].owner = -1;
        fd_conn_map.erase(conn->fd);
        epoll_ctl(epfd , EPOLL_CTL_DEL , conn->fd , nullptr);
        close(conn->fd);
        delete(conn);
        if(available)
        {
            (*available)++;
        }

    }
    else
    {
        for(auto member_fd : room_ptr[room_id].members)
        {
            if(member_fd != conn->fd)
            {
                send_string_frame(member_fd , MsgType::PARTNER_EXIT , "partner exit " + peer_to_string(conn));
            }
        }
        room_ptr[room_id].members.erase(std::remove(room_ptr[room_id].members.begin() , room_ptr[room_id].members.end() , conn->fd),room_ptr[room_id].members.end());
        epoll_ctl(epfd , EPOLL_CTL_DEL , conn->fd , nullptr);
        close(conn->fd);
        fd_conn_map.erase(conn->fd);
        delete(conn);
    }
}

static int handle_exit_meeting (Conn* conn , std::vector<Room>& room_ptr , std::unordered_map<int , Conn*>& fd_conn_map , int* available = nullptr)
{
    int room_id = conn->room_id;
    if(room_id == -1)
    {
        send_string_frame(conn->fd , MsgType::ERROR_MSG , "you are not in any room.");
        return 0;
    }
    else if(conn->fd == room_ptr[room_id].owner)
    {
        for(auto member_fd : room_ptr[room_id].members)
        {
            if(member_fd != conn->fd)
            {
                send_string_frame(member_fd , MsgType::ROOM_CLOSED , "owner exit " + peer_to_string(conn));
                fd_conn_map[member_fd]->room_id = -1;
            }
        }
        conn->room_id = -1;
        room_ptr[room_id].members.clear();
        room_ptr[room_id].owner = -1;
        if(available)
        {
            (*available)++;
        }

        return 0;
    }
    else
    {   
        for(auto member_fd : room_ptr[room_id].members)
        {
            if(member_fd != conn->fd)
            {
                send_string_frame(member_fd , MsgType::PARTNER_EXIT , "partner exit " + peer_to_string(conn));
            }
        }
        conn->room_id = -1;
        room_ptr[room_id].members.erase(
            std::remove(room_ptr[room_id].members.begin() , room_ptr[room_id].members.end(), conn->fd),
            room_ptr[room_id].members.end());
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: " << argv[0] << " port  processes \n";
        return 2;
    }

    uint16_t port = 0;
    if (parse_port(argv[1], &port) != 0) err_quit("parse port");
    int available = 0;
    if(parse_process(argv[2] , &available) != 0) err_quit("parse process");

    const int MAX_ROOM_NO = available ;
    std::vector<Room>room_ptr(available + 1);
    std::unordered_map<int , Conn*> fd_conn_map{};
    int listenfd = make_listen_socket(port);
    std::cout << "listen on 0.0.0.0:" << port << std::endl;
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    epoll_event ev{};
    ev.data.fd = listenfd;
    ev.events = EPOLLIN;
    epoll_event events[4096];
    epoll_ctl(epfd, EPOLL_CTL_ADD , listenfd , &ev);
    while(true)
    {
        int ready = epoll_wait(epfd , events , 4096 , -1);
        if(ready > 0)
        {
            for(int ev_index = 0 ; ev_index < ready ; ev_index++)
            {
                if(events[ev_index].data.fd == listenfd)
                {
                    while(true)
                    {
                        int ret = handle_new_connect(listenfd , epfd , fd_conn_map);
                        if(ret == 0)
                        {
                            continue;
                        }
                        if(errno == EINTR)
                        {
                            continue;
                        }
                        if(errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;
                        }
                        err_msg("handle new connect");
                        break;
                    }
                    continue;
                }
                else
                {
                    Frame fr{};
                    int fd = events[ev_index].data.fd;
                    int ret = read_frame(fd , fr);
                    if(ret == 1)   //正常读取
                    {
                        if(fr.type == MsgType::CREATE_MEETING)
                        {
                            if(available > 0)
                            {
                                if(handle_create_meeting(fd , fd_conn_map[fd] , room_ptr) == 0)
                                {
                                    available--;
                                }
                            }
                            else
                            {
                                send_string_frame(fd , MsgType::ERROR_MSG , "no available room");
                            }
                        }
                        else if (fr.type == MsgType::JOIN_MEETING)
                        {
                            uint32_t room_net;
                            std::memcpy(&room_net , fr.payload.data() , sizeof(room_net));
                            uint32_t room_id = ntohl(room_net);
                            int target_room_id = static_cast<int>(room_id);
                            if(target_room_id > MAX_ROOM_NO|| target_room_id <= 0)
                            {
                                send_string_frame(fd , MsgType::ERROR_MSG , "error room number");
                                continue;
                            }
                            handle_join_meeting(fd_conn_map[fd] , room_ptr , fd_conn_map , target_room_id);
                        }
                        else if(fr.type == MsgType::EXIT_MEETING)
                        {
                            handle_exit_meeting(fd_conn_map[fd] , room_ptr , fd_conn_map , &available);
                        }
                        else if(fr.type == MsgType::TEXT_SEND)
                        {
                            for(auto member_fd : room_ptr[fd_conn_map[fd]->room_id].members)
                            {
                                if(member_fd != fd)
                                {
                                    send_string_frame(member_fd , MsgType::TEXT_RECV , std::string(fr.payload.begin() , fr.payload.end()));
                                }
                            }
                        }
                    }
                    else if(ret == 0)     //断开连接
                    {
                        handle_close(fd_conn_map[fd] ,epfd , room_ptr , fd_conn_map , &available);
                    }
                    else       //异常
                    {
                        err_msg("read_frame");
                        handle_close(fd_conn_map[fd],epfd , room_ptr , fd_conn_map , &available);
                    }

                }
            }
        }
        else 
        {
            if(errno == EINTR)
            {
                continue;
            }
            err_quit("epoll_wait");
        }
    }

    close(listenfd);
    return 0;
}
