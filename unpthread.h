#pragma once
#include <vector>
#include <pthread.h>
#include <unordered_map>
#include <sys/types.h>


void Pthread_create(pthread_t* tid , pthread_attr_t * attr , void* (*func)(void*) , void* arg);
void Pthread_detach(pthread_t tid);
void Pthread_mutex_lock(pthread_mutex_t* mtx);
void Pthread_mutex_unlock(pthread_mutex_t* mtx);
void Pthread_cond_wait(pthread_cond_t* cond , pthread_mutex_t* mtx);
void Pthread_cond_signal(pthread_cond_t* cond);

struct Room
{
    pid_t process_id = 0;
    int status = 0;
    int main_pipe = -1;
    int total = 0;
};
class Manager {
public:
    explicit Manager(int room_count) 
        : used_rooms(0), room_count(room_count), rooms(room_count), next_room_no_(100001) 
    {
        pthread_mutex_init(&lock, nullptr);
    }

    void setinfo(int room_index , int pid , int sockfd)
    {
        rooms[room_index].process_id = pid;
        rooms[room_index].main_pipe = sockfd;
    }
    
    // 创建房间：返回业务房间号，不是索引
    int create_room() {
        Pthread_mutex_lock(&lock);
        if (used_rooms >= room_count) {
            Pthread_mutex_unlock(&lock);
            return -1; // 满房
        }
        
        int room_index = -1;
        for (int i = 0; i < room_count; ++i) {
            if (rooms[i].status == 0) {
                room_index = i;
                break;
            }
        }
        
        if (room_index == -1) {
            Pthread_mutex_unlock(&lock);
            return -1;
        }
        
        int room_no = next_room_no_++;
        
        // 初始化房间
        rooms[room_index].status = 1;
        rooms[room_index].total = 1;
        ++used_rooms;
        
        // 建立映射：业务号 -> 数组索引
        room_index_map[room_no] = room_index;
        
        Pthread_mutex_unlock(&lock);
        return room_no;  // 客户端拿到的是这个
    }
    
    // 加入房间：传入业务房间号
    int join_room(int room_no) {
        Pthread_mutex_lock(&lock);
        
        auto it = room_index_map.find(room_no);
        if (it == room_index_map.end()) {
            Pthread_mutex_unlock(&lock);
            return -1; // 房间不存在
        }
        
        int room_index = it->second;
        if (rooms[room_index].status != 1) {
            Pthread_mutex_unlock(&lock);
            return -1; // 房间已关闭但映射未清理（防御性）
        }
        
        // 人数上限检查
        if (rooms[room_index].total >= MAX_MEMBERS) {  // 假设最多N人
            Pthread_mutex_unlock(&lock);
            return -2; // 房间已满
        }
        
        ++rooms[room_index].total;
        
        Pthread_mutex_unlock(&lock);
        return room_no; // 返回房间号
    }
    
    //发
    void release_room_by_index(int room_index) {  
        Pthread_mutex_lock(&lock);

        if (room_index >= 0 && room_index < room_count && rooms[room_index].status == 1) {
            rooms[room_index].status = 0;
            rooms[room_index].total = 0;
            if (used_rooms > 0) --used_rooms;
        }
        
        for (auto it = room_index_map.begin(); it != room_index_map.end(); ) {
            if (it->second == room_index) {
                it = room_index_map.erase(it);
            } else {
                ++it;
            }
        }
        
        Pthread_mutex_unlock(&lock);
    }

        //成员退出(通过房间索引)
        void remove_member_by_index(int room_index)
    {
        Pthread_mutex_lock(&lock);

        if (room_index >= 0 &&
            room_index < room_count &&
            rooms[room_index].status == 1 &&
            rooms[room_index].total > 0)
        {
            --rooms[room_index].total;
        }

        Pthread_mutex_unlock(&lock);
    }


    //获取与main通信的pipe
    int getroompipe(int room_no)
    {
        Pthread_mutex_lock(&lock);

        auto it = room_index_map.find(room_no);
        int p = -1;
        if (it != room_index_map.end()) {
            p = rooms[it->second].main_pipe;
        }
        Pthread_mutex_unlock(&lock);
        return p;
    }
    
    // 内部工具：业务号转索引（其他函数用）
    int room_no_to_index(int room_no) {
        Pthread_mutex_lock(&lock);
        auto it = room_index_map.find(room_no);
        int idx = (it != room_index_map.end()) ? it->second : -1;
        Pthread_mutex_unlock(&lock);
        return idx;
    }

private:
    int used_rooms;     //已被占用的房间数量
    int room_count;     //可用的总房间数
    std::vector<Room> rooms;
    pthread_mutex_t lock;
    std::unordered_map<int, int> room_index_map;  // room_no -> room_index
    int next_room_no_;  // 自增生成器
    static constexpr int MAX_MEMBERS = 50;  
};
