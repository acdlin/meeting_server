#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <cstring>
#include "signal_util.h"
#include "unpthread.h"
#include "net.h"
#include "error.h"
#include "msg.h"


enum STATUS
{
    ON,
    OFF
};

STATUS roomstatus = ON;
SEND_QUEUE send_queue;

class room_manager
{
public:
    room_manager()
    {
        epfd = epoll_create1(EPOLL_CLOEXEC);
        owner = -1;
        pthread_mutex_init(&mtx , nullptr); 
    }
    
    room_manager(const room_manager& m) = delete;
    room_manager& operator = (const room_manager& m) = delete;
    room_manager(room_manager&& m) = delete;
    room_manager& operator = (room_manager&& m) = delete;

    ~room_manager()
    {
        this->clear();
        close(epfd);
        pthread_mutex_destroy(&mtx);
    }

    void clear()
    {
        Pthread_mutex_lock(&mtx);
        roomstatus = OFF;
        for(int fd : members)
        {
            epoll_ctl(epfd , EPOLL_CTL_DEL , fd , nullptr);
            close(fd);
        }
        owner = -1;
        room_no = 0;
        members.clear();
        fdtoip.clear();
        fdtoport.clear();
        fdbuf.clear();
        send_queue.clear();
        Pthread_mutex_unlock(&mtx);
    }

    void owner_in(int fd , int room_num)
    {
        Pthread_mutex_lock(&mtx);
        owner = fd;
        members.insert(fd);
        fdtoip.emplace(fd , getpeerip(fd));
        fdtoport.emplace(fd, getpeerport(fd));
        roomstatus = ON;
        add_fd_to_epoll(fd);
        room_no = room_num;
        Pthread_mutex_unlock(&mtx);
    }

void member_in(int fd)
{
    Pthread_mutex_lock(&mtx);
    add_fd_to_epoll(fd);
    members.insert(fd);
    uint32_t ip = getpeerip(fd);
    uint16_t port = getpeerport(fd);
    fdtoip.emplace(fd, ip);
    fdtoport.emplace(fd, port);

    MSG msg{};
    msg.type = MsgType::PARTNER_JOIN;
    msg.targetfd = fd;
    msg.ip_net = ip;
    msg.payload.assign(reinterpret_cast<const char *>(&port), sizeof(port));
    send_queue.push_queue(msg);

    MSG msg1{};
    msg1.type = MsgType::PARTNER_JOIN2;
    msg1.targetfd = fd;
    msg1.ip_net = ip;

    for(auto& pair : fdtoip)
    {
        if(pair.first != fd)
        {
            uint32_t member_ip = pair.second;
            msg1.payload.append(reinterpret_cast<const char *>(&member_ip), sizeof(member_ip));
            uint16_t member_port = fdtoport[pair.first];
            msg1.payload.append(reinterpret_cast<const char *>(&member_port), sizeof(member_port));
        }
    }

    send_queue.push_queue(msg1);

    Pthread_mutex_unlock(&mtx);
}

    void add_fd_to_epoll(int fd )
    {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = fd;
        if(epoll_ctl(epfd , EPOLL_CTL_ADD , fd , &ev))
        {
            err_quit("add fd to epfd error");
        }
    }

    int get_room_no()
    {
        return room_no;
    }

    uint32_t getip(int fd)
    {
        Pthread_mutex_lock(&mtx);
        auto it = fdtoip.find(fd);
        uint32_t ip = (it == fdtoip.end()) ? 0 : it->second;
        Pthread_mutex_unlock(&mtx);
        return ip;
    }


    std::vector<int> targets_except(int except_fd)
    {
        std::vector<int> targets;

        Pthread_mutex_lock(&mtx);
        for (int fd : members)
        {
            if (fd != except_fd)
            {
                targets.push_back(fd);
            }
        }
        Pthread_mutex_unlock(&mtx);

        return targets;
    }

    std::vector<int> target_only(int target_fd)
    {
        std::vector<int> targets;

        Pthread_mutex_lock(&mtx);
        if (members.count(target_fd) > 0)
        {
            targets.push_back(target_fd);
        }
        Pthread_mutex_unlock(&mtx);

        return targets;
    }

    void event_loop(int pipefd)
    {
        std::vector<epoll_event> events(64);
        while(true)
        {
            int ret = epoll_wait(epfd , events.data() , events.size() , -1);
            if(ret < 0)
            {
                if(errno == EINTR) continue;
                err_quit("epoll wait error");
            }
            for(int i = 0 ; i < ret ; i++)
            {
                int clientfd = events[i].data.fd;
                handle_client_read(clientfd , pipefd);
            }
        }
    }

    void handle_client_read(int clientfd, int pipefd)
    {
        char tmp[4096];
        ssize_t n = read(clientfd, tmp, sizeof(tmp));
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            fdclose(clientfd, pipefd);
            return;
        }
        if (n == 0)
        {
            fdclose(clientfd, pipefd);
            return;
        }

        auto& buf = fdbuf[clientfd];
        buf.insert(buf.end(), tmp, tmp + n);

        while (true)
        {
            size_t avail = buf.size();
            if (avail < 11)
                return;

            char* p = buf.data();
            if (p[0] != '$')
            {
                err_msg("msg format error");
                fdclose(clientfd, pipefd);
                return;
            }

            MsgType type;
            memcpy(&type, p + 1, sizeof(uint16_t));
            type = static_cast<MsgType>(ntohs(static_cast<uint16_t>(type)));

            uint32_t payload_len_net = 0;
            memcpy(&payload_len_net, p + 7, sizeof(payload_len_net));
            uint32_t payload_len = ntohl(payload_len_net);

            size_t frame_len = 11 + payload_len + 1;
            if (avail < frame_len)
                return;  // 帧不完整，等下次数据

            if (p[frame_len - 1] != '#')
            {
                err_msg("tail format error");
                fdclose(clientfd, pipefd);
                return;
            }

            MSG msg{};
            msg.targetfd = clientfd;
            msg.ip_net = getip(clientfd);

            bool consumed = false;

            if (type == MsgType::IMG_SEND || type == MsgType::AUDIO_SEND || type == MsgType::TEXT_SEND)
            {
                if (type == MsgType::IMG_SEND) msg.type = MsgType::IMG_RECV;
                else if (type == MsgType::AUDIO_SEND) msg.type = MsgType::AUDIO_RECV;
                else msg.type = MsgType::TEXT_RECV;

                msg.payload.assign(p + 11, payload_len);
                send_queue.push_queue(msg);
                consumed = true;
            }
            else if (type == MsgType::CLOSE_CAMERA)
            {
                if (payload_len == 0)
                {
                    msg.type = MsgType::CLOSE_CAMERA;
                    uint16_t port = getpeerport(clientfd);
                    uint16_t port_net = htons(port);
                    msg.payload.assign(reinterpret_cast<const char*>(&port_net), sizeof(port_net));
                    send_queue.push_queue(msg);
                    consumed = true;
                }
                else
                {
                    err_msg("close camera format error");
                    consumed = true;
                }
            }
            else if (type == MsgType::EXIT_MEETING)
            {
                if (payload_len == 0)
                {
                    consumed = true;
                    buf.erase(buf.begin(), buf.begin() + frame_len);
                    fdclose(clientfd, pipefd);
                    return;
                }
                else
                {
                    err_msg("exit meeting format error");
                    consumed = true;
                }
            }
            else
            {
                err_msg("unknown room msg type");
                fdclose(clientfd, pipefd);
                return;
            }

            if (consumed)
                buf.erase(buf.begin(), buf.begin() + frame_len);
        }
    }

void fdclose(int fd , int pipefd)
{
    if(fd == owner)
    {
        clear();
        char cmd = 'E';
        writen(pipefd , &cmd , 1);
        return ;
    }


    uint32_t ip = getip(fd);

    Pthread_mutex_lock(&mtx);
    uint16_t port = 0;
    auto it = fdtoport.find(fd);
    if(it != fdtoport.end())
    {
        port = it->second;
    }
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    members.erase(fd);
    fdtoip.erase(fd);
    fdtoport.erase(fd);
    fdbuf.erase(fd);
    Pthread_mutex_unlock(&mtx);

    close(fd);

    char cmd = 'Q';
    writen(pipefd, &cmd, 1);

    MSG msg{};
    msg.type = MsgType::PARTNER_EXIT;
    msg.targetfd = fd;
    msg.ip_net = ip;
    msg.payload.assign(reinterpret_cast<const char *>(&port), sizeof(port));
    send_queue.push_queue(msg);
    std::cout << "exit msg sended\n";
}
private:
    int epfd{};
    int owner{};
    int room_no{};
    pthread_mutex_t mtx{};
    std::unordered_set<int> members{};
    std::unordered_map<int , uint32_t> fdtoip{};
    std::unordered_map<int , uint16_t> fdtoport{};
    std::unordered_map<int, std::vector<char>> fdbuf{};
};

const int MB = 1024 * 1024;
const int send_thread_size = 5;

room_manager* room_ma = new room_manager();

extern Manager* manager;

void process_main(int i , int fd)
{
    std::cout << "room [" << getpid() << "]: starting \n";
    Signal(SIGPIPE , SIG_IGN);
    pthread_t pfd;
    void* accept_fd(void*);
    void* send_func(void*);
    void fdclose(int , int);
    int* ptr = new int;
    *ptr = fd;
    Pthread_create(&pfd , nullptr , accept_fd , ptr);
    pthread_t temp;
    for(int i = 0 ; i < send_thread_size ; i++)
    {
        Pthread_create(&temp , nullptr , send_func , nullptr);
    }
    room_ma->event_loop(fd);
}


void* accept_fd(void* arg)
{
    Pthread_detach(pthread_self());
    int fd = *(int*) arg;
    RoomCmd cmd{};
    int tfd = -1;
    delete (int *)arg;
    while(true)
    {
        recv_fd(fd , &cmd , sizeof(cmd) , &tfd);
        if(tfd < 0)
        {
            err_quit("recv fd error");
        }
        if(cmd.cmd == 'C')
        {
            int room_no = ntohl(cmd.room_no_net);
            room_ma->owner_in(tfd , room_no);
            MSG msg;
            msg.type = MsgType::CREATE_MEETING_RESPONSE;
            msg.targetfd = tfd;
            msg.payload.assign(reinterpret_cast<const char*>(&cmd.room_no_net), sizeof(uint32_t));
            send_queue.push_queue(msg);
        }
        else if(cmd.cmd == 'J')
        {
            if(roomstatus == OFF)
            {
                close(tfd);
                continue;
            }
            room_ma->member_in(tfd);
            std::cout << "Room" << room_ma->get_room_no() <<" :member in.\n";
        }
    }
    return nullptr;
}

void* send_func(void* arg)
{
    Pthread_detach(pthread_self());
    char* buf = new char[4 * MB];

    while(true)
    {
        MSG msg = send_queue.pop_queue();
        int len = 0;
        buf[len++] = '$';
        short type = htons((short)msg.type);
        memcpy(buf + len , &type , sizeof(type));
        len += 2;

        if(msg.type == MsgType::CREATE_MEETING_RESPONSE || msg.type == MsgType :: PARTNER_JOIN2)
        {
            uint32_t zero_ip = 0;
            memcpy(buf + len, &zero_ip, sizeof(zero_ip));
            len += sizeof(zero_ip);
        }
        else if(msg.type == MsgType::TEXT_RECV || msg.type == MsgType::PARTNER_EXIT || msg.type == MsgType::PARTNER_JOIN || msg.type == MsgType::IMG_RECV || msg.type == MsgType::AUDIO_RECV || msg.type == MsgType::CLOSE_CAMERA)
        {
            uint32_t ip_net = msg.ip_net;
            memcpy(buf + len , &ip_net , sizeof(ip_net));
            len += sizeof(ip_net);
        }
        uint32_t msglen = htonl(static_cast<uint32_t>(msg.payload.size()));
        memcpy(buf + len , &msglen , sizeof(msglen));
        len += sizeof(msglen);
        memcpy(buf + len , msg.payload.data() , msg.payload.size());
        len += msg.payload.size();
        buf[len++] = '#';

    std::vector<int> targets;

    if (msg.type == MsgType::CREATE_MEETING_RESPONSE || msg.type == MsgType::PARTNER_JOIN2)
    {
        targets = room_ma->target_only(msg.targetfd);
    }
    else if (msg.type == MsgType::TEXT_RECV || msg.type == MsgType::PARTNER_EXIT || msg.type == MsgType::PARTNER_JOIN ||msg.type == MsgType::IMG_RECV || msg.type == MsgType::AUDIO_RECV || msg.type == MsgType::CLOSE_CAMERA)
    {
        targets = room_ma->targets_except(msg.targetfd);
    }

    for (int fd : targets)
    {
        ssize_t wret = writen(fd, buf, len);
        if (wret < 0)
        {
            err_msg("writen error");
        }
    }
    }
    delete [] buf;
    return nullptr;
}

