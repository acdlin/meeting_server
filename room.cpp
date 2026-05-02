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
        send_queue.clear();
        Pthread_mutex_unlock(&mtx);
    }

    void owner_in(int fd , int room_num)
    {
        Pthread_mutex_lock(&mtx);
        owner = fd;
        members.insert(fd);
        fdtoip.emplace(fd , getpeerip(fd));
        roomstatus = ON;
        add_fd_to_epoll(fd);
        room_no = room_num;
        Pthread_mutex_unlock(&mtx);
    }

    void member_in(int fd )
    {
        Pthread_mutex_lock(&mtx);
        add_fd_to_epoll(fd);
        members.insert(fd);
        fdtoip.emplace(fd , getpeerip(fd));
        MSG msg{};
        msg.type = MsgType::PARTNER_JOIN;
        msg.targetfd = fd;
        msg.ip_net = fdtoip[fd];
        send_queue.push_queue(msg);
        MSG msg1{};
        msg1.type = MsgType::PARTNER_JOIN2;
        msg1.targetfd = fd;
        msg1.ip_net = fdtoip[fd];
        for(auto& [key, ip_net] : fdtoip)
        {
            if(ip_net != fdtoip[fd])
            {
                msg1.payload.append(reinterpret_cast<const char *>(&ip_net) , sizeof(ip_net));
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

    void handle_client_read(int clientfd , int pipefd)
    {
        char head[11] = {0};
        int ret = readn(clientfd , head , 11);
        if(ret <= 0)
        {
            fdclose(clientfd , pipefd);
            return;
        }

        if(ret != 11 || head[0] != '$')
        {
            err_msg("msg format error");
            return;
        }

        MsgType type;
        memcpy(&type , head + 1 , sizeof(uint16_t));
        type = static_cast<MsgType>(ntohs(static_cast<uint16_t>(type)));

        uint32_t payload_len_net = 0;
        memcpy(&payload_len_net, head + 7, sizeof(payload_len_net));
        uint32_t payload_len = ntohl(payload_len_net);

        MSG msg{};
        msg.targetfd = clientfd;
        msg.ip_net = getip(clientfd);

    if (type == MsgType::IMG_SEND || type == MsgType::AUDIO_SEND || type == MsgType::TEXT_SEND)
    {
        if (type == MsgType::IMG_SEND) msg.type = MsgType::IMG_RECV;
        else if (type == MsgType::AUDIO_SEND) msg.type = MsgType::AUDIO_RECV;
        else msg.type = MsgType::TEXT_RECV;

        msg.payload.resize(payload_len);

        if (readn(clientfd, msg.payload.data(), payload_len) != static_cast<ssize_t>(payload_len))
        {
            err_msg("payload read error");
            return;
        }

        char tail = 0;
        if (readn(clientfd, &tail, 1) != 1 || tail != '#')
        {
            err_msg("tail format error");
            return;
        }

        send_queue.push_queue(msg);
    }
    else if (type == MsgType::CLOSE_CAMERA)
    {
        char tail = 0;
        if (payload_len == 0 && readn(clientfd, &tail, 1) == 1 && tail == '#')
        {
            msg.type = MsgType::CLOSE_CAMERA;
            send_queue.push_queue(msg);
        }
        else
        {
            err_msg("close camera format error");
        }
    }
    else if (type == MsgType::EXIT_MEETING)
    {
        char tail = 0;

        if (payload_len == 0 &&
            readn(clientfd, &tail, 1) == 1 &&
            tail == '#')
        {
            fdclose(clientfd, pipefd);
        }
        else
        {
            err_msg("exit meeting format error");
        }
    }
    else
    {
        err_msg("unknown room msg type");
        fdclose(clientfd, pipefd);
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
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    members.erase(fd);
    fdtoip.erase(fd);
    Pthread_mutex_unlock(&mtx);

    close(fd);

    char cmd = 'Q';
    writen(pipefd, &cmd, 1);

    MSG msg{};
    msg.type = MsgType::PARTNER_EXIT;
    msg.targetfd = fd;
    msg.ip_net = ip;
    send_queue.push_queue(msg);
}
private:
    int epfd{};
    int owner{};
    int room_no{};
    pthread_mutex_t mtx{};
    std::unordered_set<int> members{};
    std::unordered_map<int , uint32_t> fdtoip{};
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
        if (writen(fd, buf, len) < 0)
        {
            err_msg("writen error");
        }
    }
    }
    delete [] buf;
    return nullptr;
}

