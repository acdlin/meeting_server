#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <sys/socket.h>
    
enum class MsgType : uint16_t
{
    IMG_SEND = 0,
    IMG_RECV,
    AUDIO_SEND,
    AUDIO_RECV,
    TEXT_SEND,
    TEXT_RECV,
    CREATE_MEETING,
    JOIN_MEETING,
	CLOSE_CAMERA,
    EXIT_MEETING,

    CREATE_MEETING_RESPONSE = 20,
    PARTNER_EXIT = 21,
    PARTNER_JOIN = 22,
    JOIN_MEETING_RESPONSE = 23,
    PARTNER_JOIN2 = 24
};


enum class Image_Format : uint16_t
{
    Format_Invalid,
    Format_Mono,
    Format_MonoLSB,
    Format_Indexed8,
    Format_RGB32,
    Format_ARGB32,
    Format_ARGB32_Premultiplied,
    Format_RGB16,
    Format_ARGB8565_Premultiplied,
    Format_RGB666,
    Format_ARGB6666_Premultiplied,
    Format_RGB555,
    Format_ARGB8555_Premultiplied,
    Format_RGB888,
    Format_RGB444,
    Format_ARGB4444_Premultiplied,
    Format_RGBX8888,
    Format_RGBA8888,
    Format_RGBA8888_Premultiplied,
    Format_BGR30,
    Format_A2BGR30_Premultiplied,
    Format_RGB30,
    Format_A2RGB30_Premultiplied,
    Format_Alpha8,
    Format_Grayscale8,
    Format_RGBX64,
    Format_RGBA64,
    Format_RGBA64_Premultiplied,
    Format_Grayscale16,
    Format_BGR888,
    NImageFormats
};

struct RoomCmd
{
    char cmd;
    uint32_t room_no_net;
};


int parse_port(const char* start , uint16_t * port);
int parse_process(const char* start , int * processes);
void Socketpair(int domain , int type , int protocol , int* sock);
ssize_t readn(int fd , void* buf , size_t n);
ssize_t writen(int fd ,const void* buf ,size_t n);
int make_listen_socket(uint16_t port , socklen_t * addrlen );
int Epoll_create(int flags);
void write_fd(int sockfd , const void *ptr , size_t nbytes , int fdtosend);
void recv_fd(int sockfd , void* ptr , size_t nbytes , int* fd);
const char* Sock_ntop(char* str, int size, const sockaddr* sa, socklen_t salen);
int Accept(int listenfd, sockaddr* addr, socklen_t *addrlen);
uint32_t getpeerip(int connfd );
uint16_t getpeerport(int connfd);

