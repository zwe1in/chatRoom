#include <stdio.h>
#include <iostream>
#include <stdint.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <queue>
#include <pthread.h>

using namespace std;

#define BUFFER_LEN 1024
#define MAX_CLIENT_LEN 50
#define NAME_LEN 30
#define MAX_ADDER_LEN 10

struct Client
{
    bool valid;
    uint32_t uid;
    uint32_t socket;
    char name[NAME_LEN + 1];
} clients[MAX_CLIENT_LEN];

// 在线人数互斥位
int current_member = 0;
pthread_mutex_t num_mutex = PTHREAD_MUTEX_INITIALIZER;
// 新增用户线程互斥位
int current_adder = 0;
pthread_mutex_t adder_mutex = PTHREAD_MUTEX_INITIALIZER;

queue<string> message_queue[MAX_CLIENT_LEN];

pthread_t chat_thread[MAX_CLIENT_LEN];
pthread_t send_thread[MAX_CLIENT_LEN];

// 每个client用户专用的互斥
pthread_mutex_t mutex[MAX_CLIENT_LEN];
pthread_cond_t ct[MAX_CLIENT_LEN];


void recvMsg(void* data)
{
    Client* client = (Client*)data;
    string strBuffer;

    char buffer[BUFFER_LEN];
    int buffer_len = 0;

    while((buffer_len = recv(client->socket, buffer, sizeof(buffer), 0)) > 0)
    {
        
        for(int i = 0; i < buffer_len; ++i)
        {
            if(strBuffer.size() == 0)
                strBuffer = string(client->name) + ": ";
            strBuffer += buffer[i];

            // 找换行符'\n'，找到后广播
            if(buffer[i] == '\n')
            {
                for(int i = 0; i < MAX_CLIENT_LEN; ++i)
                {
                    if(clients[i].valid)
                    {
                        pthread_mutex_lock(&mutex[clients[i].uid]);
                        message_queue[clients[i].uid].push(strBuffer);
                        pthread_cond_signal(&ct[clients[i].uid]);
                        pthread_mutex_unlock(&mutex[clients[i].uid]);
                    }
                }
                strBuffer.clear();
            }
        }
        buffer_len = 0;
        memset(buffer, 0, sizeof(buffer));
    }

}

void* sendMsg(void* data)
{
    Client *client = (Client*)data;
    while(1)
    {
        pthread_mutex_lock(&mutex[client->uid]);
        // 消息队列为空，等待内容
        while(message_queue[client->uid].empty())
        {
            pthread_cond_wait(&ct[client->uid], &mutex[client->uid]);
        }
        // 消息队列有缓冲数据，发送
        while(!message_queue[client->uid].empty())
        {
            string message = message_queue[client->uid].front();
            int size = message.size();
            int trans_len = size > BUFFER_LEN ? BUFFER_LEN : size;
            while(size > 0)
            {
                int len = send(client->socket, message.c_str(), trans_len, 0);
                if(len < 0)
                {
                    perror("Send error..");
                    return NULL;
                }
                size -= len;
                message.erase(0, len);
                trans_len = size > BUFFER_LEN ? BUFFER_LEN : size;
            }
            message.clear();
            message_queue[client->uid].pop();
        }
        pthread_mutex_unlock(&mutex[client->uid]);
    }
    return NULL;
}

void* chat(void* data)
{
    Client* client = (Client*) data;

    // 创建发送线程
    pthread_create(&send_thread[client->uid], NULL, sendMsg, (void*)client);

    // 向新用户发送欢迎
    char hello[100];
    sprintf(hello, "Hello %s, Welcome to join the chatroom. Online User Member: %d\n", client->name, current_member);
    pthread_mutex_lock(&mutex[client->uid]);
    message_queue[client->uid].push(hello);
    pthread_cond_signal(&ct[client->uid]);
    pthread_mutex_unlock(&mutex[client->uid]);

    // 广播
    memset(hello, 0, sizeof(hello));
    sprintf(hello, "New User %s join in! Online User Member: %d\n", client->name, current_member);
    for(int i = 0; i < MAX_CLIENT_LEN; ++i)
    {
        if(clients[i].valid)
        {
            pthread_mutex_lock(&mutex[clients[i].uid]);
            message_queue[i].push(hello);
            pthread_cond_signal(&ct[clients[i].uid]);
            pthread_mutex_unlock(&mutex[clients[i].uid]);
        }
    }


    // 接收
    recvMsg(data);

    // 结束
    pthread_mutex_lock(&num_mutex);
    client->valid = false;
    current_member--;
    pthread_mutex_unlock(&num_mutex);
    // 广播离开消息
    char bye[100];
    sprintf(bye, "%s left chatRoom. Online Member: %d", client->name, current_member);
    for(int i = 0; i < MAX_CLIENT_LEN; ++i)
    {
        if(clients[i].valid)
        {
            pthread_mutex_lock(&mutex[clients[i].uid]);
            message_queue[clients[i].uid].push(bye);
            pthread_cond_signal(&ct[clients[i].uid]);
            pthread_mutex_unlock(&mutex[clients[i].uid]);
        }
    }

    pthread_mutex_destroy(&mutex[client->uid]);
    pthread_cond_destroy(&ct[client->uid]);
    pthread_cancel(send_thread[client->uid]);

    return NULL;
}

void* addMember(void* data)
{
    int socketClient = *(int*)data;
    // 没有空位
    if(current_member >= MAX_CLIENT_LEN)
    {
        if(send(socketClient, "FULL", sizeof("FULL"), 0) < 0)
            perror("Send error..");
        shutdown(socketClient, SHUT_RDWR);
        return NULL;
    }

    // 有空位，锁定公共资源
    if(send(socketClient, "OK", sizeof("OK"), 0) < 0)
    {
        perror("Send error..");
        shutdown(socketClient, SHUT_RDWR);
        return NULL;
    }
    pthread_mutex_lock(&num_mutex);

    // 接收返回的用户名
    char name[NAME_LEN + 1];
    ssize_t state = recv(socketClient, name, NAME_LEN, 0);
    if(state <= 0)
    {
        perror("Recv error..");
        shutdown(socketClient, SHUT_RDWR);
        return NULL;
    }
    
    // 更新在线用户名单
    for(int i = 0; i < MAX_CLIENT_LEN; ++i)
    {
        if(!clients[i].valid)
        {
            memset(clients[i].name, 0, sizeof(clients[i].name));
            strcpy(clients[i].name, name);
            clients[i].valid = true;
            clients[i].uid = i;
            clients[i].socket = socketClient;

            mutex[i] = PTHREAD_MUTEX_INITIALIZER;
            ct[i] = PTHREAD_COND_INITIALIZER;

            current_member++;
            pthread_mutex_unlock(&num_mutex);

            // 开启该用户的聊天线程,并且从当前线程分离出去
            pthread_create(&chat_thread[i], NULL, chat, (void*)&clients[i]);
            pthread_detach(chat_thread[i]);
            printf("%s join the ChatRoom. Current Online Member: %d\n", name, current_member);
            break;
        }
    }

    pthread_mutex_lock(&adder_mutex);
    current_adder--;
    pthread_mutex_unlock(&adder_mutex);
}

int main(int argc, char **argv)
{
    if(argc != 2)
    {
        cout << "Please use in format: \"./Server port\" " << endl;
        return 1;
    }
    uint16_t port = atoi(argv[1]);
    int socketServer;
    if((socketServer = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket error..");
        return 1;
    }    

    sockaddr_in addrServer;
    addrServer.sin_family = AF_INET;
    addrServer.sin_addr.s_addr = INADDR_ANY;
    addrServer.sin_port = htons(port);
    if(bind(socketServer, (sockaddr*)&addrServer, sizeof(addrServer)))
    {
        perror("Bind error..");
        return 1;
    }

    if(listen(socketServer, MAX_CLIENT_LEN + 1))
    {
        perror("Listen error..");
        return 1;
    }
    cout << "Server star successfully. Port Member: " << port << " ip: 127.0.0.1" << endl;

    while(1)
    {
        sockaddr_in addrClient;
        socklen_t addrClientLen;
        int socketClient = accept(socketServer, (sockaddr*)&addrClient, &addrClientLen);
        if(socketClient == -1)
        {
            perror("Accept error..");
            return 1;
        }
        pthread_mutex_lock(&adder_mutex);
        if(current_adder >= MAX_ADDER_LEN)
        {
            pthread_mutex_unlock(&adder_mutex);
            continue;
        }
        else
        {
            current_adder++;
            pthread_mutex_unlock(&adder_mutex);
        }
        int *sctp = &socketClient;
        pthread_t tid;
        pthread_create(&tid, NULL, addMember, (void*)sctp);
        
    }

    // 循环结束，服务端退出
    for(int i = 0; i < MAX_CLIENT_LEN; ++i)
    {
        if(clients[i].valid)
            shutdown(clients[i].socket, SHUT_RDWR);
    }
    shutdown(socketServer, SHUT_RDWR);

    return 0;
} 

