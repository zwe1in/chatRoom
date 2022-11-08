#include <stdio.h>
#include <string.h>
#include <iostream>
#include <string>
#include <sstream>
#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

using namespace std;

#define NAME_LEN 20
#define BUFFER_LEN 1024


void* recvMsg(void* data)
{
    int socketClient = *(int*)data;

    string msgStr = "";

    char buffer[BUFFER_LEN + 1];
    int len;

    while((len = recv(socketClient, buffer, sizeof(buffer), 0)) > 0)
    {
        for(int i = 0; i < len; ++i)
        {
            if(buffer[i] == '\n')
            {
                cout << msgStr << endl;
                msgStr.clear();
            }
            msgStr += buffer[i];
        }
        memset(buffer, 0, sizeof(buffer));
    }
}


int main()
{
    int socketClient;
    if((socketClient = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket error..");
        return 1;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;

    uint16_t port = 0;
    string ip;
    while(1)
    {
        cout << "Please enter the server IP: " << endl;
        cin >> ip;
        cout << "Please enter the port: " << endl;
        cin >> port; 

        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        addr.sin_port = htons(port);

        if(connect(socketClient, (sockaddr*)&addr, sizeof(addr)))
        {
            perror("Connect error");
            return 1;
        }
        break;
    }

    cout << "Connecting ......" << endl;
    char state[20];
    if(recv(socketClient, state, sizeof(state), 0) < 0)
    {
        perror("Recv error..");
        return 1;
    }

    if(strcmp(state, "OK"))
    {
        cout << "The ChatRoom has been full." << endl;
        return 0;
    }
    else
    {
        cout << "Connect the chatRoom successfully." << endl;
    }

    // 输入用户名
    cout << "Welcome!! " << endl;
    while(1)
    {
        cout << "Please enter your name: " << endl;
        string name;
        cin >> name;
        if(name.size() > NAME_LEN)
        {
            cout << "Your name should be shorter than 20 letters. " << endl;
            continue;
        }
        else if(name.size() == 0)
        {
            cout << "Your name should have at least 1 letter. " << endl;
            continue;
        }
        if(send(socketClient, name.c_str(), name.size(), 0) < 0)
        {
            perror("Send error..");
            continue;
        }
        break;
    }

    // 接受消息线程
    pthread_t tid;
    pthread_create(&tid, NULL, recvMsg, (void*)&socketClient);
    // 发送消息处理
    while(1)
    {
        string message;
        getline(cin, message);
        if(message.size() == 0)
            continue;
        message += "\n";
        int size = message.size();
        int trans_len = BUFFER_LEN > size ? size : BUFFER_LEN;
        while(size > 0)
        {
            int len = send(socketClient, message.c_str(), trans_len, 0);
            if(len < 0)
            {
                perror("Send error..");
                return 1;
            }
            size -= len;
            message.erase(0, len);
            trans_len = BUFFER_LEN > size ? size : BUFFER_LEN;
        }
    }

    pthread_cancel(tid);
    shutdown(socketClient, SHUT_RDWR);
    return 0;
}
