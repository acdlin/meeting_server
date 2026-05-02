#pragma once
#include "unpthread.h"
#include "net.h"
#include <queue>
#include <pthread.h>

struct MSG
{
    MsgType type{};
    uint32_t ip_net{};
    int targetfd{};
    std::string payload{};
    Image_Format format{};
};

class SEND_QUEUE
{
private:
    pthread_mutex_t lock{};
    pthread_cond_t cond{};
    std::queue<MSG> send_queue{};
public:
    SEND_QUEUE()
    {
        pthread_mutex_init(&lock , nullptr);
        pthread_cond_init(&cond , nullptr);
    }
    ~SEND_QUEUE()
    {
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
    }

    void push_queue(const MSG& m)
    {
        Pthread_mutex_lock(&lock);
        send_queue.push(m);
        Pthread_mutex_unlock(&lock);
        Pthread_cond_signal(&cond);
    }
    MSG pop_queue()
    {
        Pthread_mutex_lock(&lock);
        while(send_queue.empty())
        {
            Pthread_cond_wait(&cond , &lock);
        }
        MSG m = send_queue.front();
        send_queue.pop();
        Pthread_mutex_unlock(&lock);
        return m;
    }

    void clear()
    {
        std::queue<MSG> old;
        Pthread_mutex_lock(&lock);
        send_queue.swap(old);
        Pthread_mutex_unlock(&lock);
    }
    SEND_QUEUE(const SEND_QUEUE& ) = delete;
    SEND_QUEUE& operator = (const SEND_QUEUE&) = delete;
};