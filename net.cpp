#include "net.h"
#include <iostream>
#include <cstdint>
#include <vector>
#include <unistd.h>
#include <errno.h>
#include <cstddef>
#include <arpa/inet.h>
#include <cstring>
#include <climits>

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


//读满n个字符，返回读取的字符数量
ssize_t readn(int fd , void* buf , size_t n)
{
    size_t remain  = n;
    char * p = static_cast<char *>(buf);
    while(remain > 0)
    {
        ssize_t r = ::read(fd , p , remain);
        if(r < 0 )
        {
            if(errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break;
        remain -= r;
        p +=r;
    }
    return static_cast<ssize_t>(n - remain);
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
            return -1;
        } 
        if(w == 0) break;

        remain -= w;
        p += w;
    }
    return static_cast<ssize_t>(n - remain);
}


//返回是否读取正确数量的字符的读取函数
static bool read_exact(int fd , void* buf ,size_t n)
{
    ssize_t r = readn(fd , buf , n);
    return r == static_cast<ssize_t>(n);

}


//将frame发送出去的函数
int send_frame(int fd , const Frame& fr)
{
    std::vector<uint8_t> buf;
    buf.reserve(11 + fr.payload.size() + 1);

    buf.push_back('$');

    uint16_t type_net = htons(static_cast<uint16_t>(fr.type));
    uint32_t len_net = htonl(static_cast<uint32_t>(fr.payload.size()));

    buf.insert(buf.end(),reinterpret_cast<uint8_t*>(&type_net),reinterpret_cast<uint8_t*>(&type_net) + sizeof(type_net));
    buf.insert(buf.end(),reinterpret_cast<const uint8_t*>(&fr.ip_net),reinterpret_cast<const uint8_t*>(&fr.ip_net) + sizeof(fr.ip_net));
    buf.insert(buf.end(),reinterpret_cast<uint8_t*>(&len_net), reinterpret_cast<uint8_t*>(&len_net) + sizeof(len_net));
    
    buf.insert(buf.end() , fr.payload.begin() , fr.payload.end());
    buf.push_back('#');
    return (writen(fd , buf.data() , buf.size()) == static_cast<ssize_t>(buf.size()) ? 0 : -1);
}

//读取frame的函数（成功1 ， 对端关闭 0 ， 失败-1）
int read_frame(int fd , Frame& fr)
{
    uint8_t head[11];
    ssize_t r = readn(fd , head, sizeof(head));
    if(r == 0) return 0;
    if(r != static_cast<ssize_t>(sizeof(head))) return -1;

    if(head[0] != '$') return -1;

    uint16_t type_net;
    std::memcpy(&type_net , head + 1 , 2);
    uint16_t type_host = ntohs(type_net);

    uint32_t ip_net;
    std::memcpy(&ip_net , head +3 , 4);


    uint32_t len_net;
    std::memcpy(&len_net ,head + 7 , 4);
    uint32_t len_host = ntohl(len_net);

    const uint32_t MAX_PAYLOAD = 4 * 1024 * 1024;
    if( len_host > MAX_PAYLOAD) return -1;

    std::vector<uint8_t> payload;
    payload.resize(len_host);

    if(len_host > 0)
    {
        if(!read_exact(fd , payload.data() , len_host)) return -1;
    }
    uint8_t tail;
    if(!read_exact(fd , &tail , 1)) return -1;
    if(tail != '#') return -1;

    fr.type = static_cast<MsgType>(type_host);
    fr.ip_net = ip_net;
    fr.payload = std::move(payload);
    return 1;
}

//将字符发出的函数（带类型的帧）
int send_string_frame(int fd , MsgType type , const std::string & msg )
{
    Frame fr{};
    fr.payload.assign(msg.begin() , msg.end());
    fr.type = type;
    return send_frame(fd , fr);
}


//专门发无符号32位的函数
int send_u32_frame(int fd , MsgType type , uint32_t value , uint32_t ip_net)
{
    Frame fr{};
    fr.ip_net = ip_net;
    fr.payload.resize(4);
    uint32_t net = htonl(value);
    memcpy(fr.payload.data() , &net , 4);
    fr.type = type;
    return send_frame(fd , fr);
}

