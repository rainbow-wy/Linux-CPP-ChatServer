#ifndef CLIENT_SESSION_H
#define CLIENT_SESSION_H

#include <string>

struct ClientSession {

    // 接收缓冲区
    std::string recvBuffer;

    // 发送缓冲区
    std::string sendBuffer;

};

#endif