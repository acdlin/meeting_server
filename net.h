#pragma once
#include <cstdint>
#include <vector>
#include <string>
    
enum class MsgType : uint16_t
{
    TEXT_SEND = 4,
    TEXT_RECV = 5,
    CREATE_MEETING = 6,
    JOIN_MEETING = 7,
    EXIT_MEETING = 8,
};

struct Frame
{
    MsgType type{};
    uint32_t ip_net{};
    std::vector<uint8_t> payload;
};

int parse_port(const char* start , uint16_t * port);
int send_frame(int fd , const Frame & fr);
int read_frame(int fd , Frame & fr);
int send_text(int fd , const std::string& msg , uint32_t ip_net = 0);
