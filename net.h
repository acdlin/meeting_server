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
    CREATE_MEETING_RESPONSE = 9,
    JOIN_MEETING_RESPONSE = 10,
    EXIT_MEETING_RESPONSE = 11,
    ERROR_MSG = 12,
    ROOM_CLOSED = 13,
    PARTNER_JOIN = 14,
    PARTNER_EXIT = 15,
};

struct Frame
{
    MsgType type{};
    uint32_t ip_net{};
    std::vector<uint8_t> payload;
};

int parse_port(const char* start , uint16_t * port);
int parse_process(const char* start , int * processes);
int send_frame(int fd , const Frame & fr);
int read_frame(int fd , Frame & fr);
int send_string_frame(int fd , MsgType type , const std::string & msg);
int send_u32_frame(int fd , MsgType type , uint32_t value , uint32_t ip_net = 0);

