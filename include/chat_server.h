
#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

#include <cstdint>
#include <string>
#include <unordered_map>

#include "client_session.h"

class ChatServer {
public:

    // 构造函数
    explicit ChatServer(int port);

    // 析构函数
    ~ChatServer();

    // 初始化服务器
    bool init();

    // 启动服务器事件循环
    void run();

    // 禁止拷贝，避免重复管理同一批文件描述符
    ChatServer(const ChatServer&) = delete;
    ChatServer& operator=(const ChatServer&) = delete;

private:

    // 服务器端口
    int port_;

    // 监听 Socket
    int listenFd_ = -1;

    // epoll 文件描述符
    int epollFd_ = -1;

    // 管理所有客户端及其状态
    std::unordered_map<int, ClientSession> clients_;

    // epoll 每次最多处理的事件数量
    static constexpr int MAX_EVENTS = 64;

    // 接受新客户端连接
    void acceptClients();

    // 处理客户端就绪事件
    void handleClientEvent(int fd, std::uint32_t revents);

    // 处理可读事件
    void handleRead(int fd);

    // 处理可写事件
    void handleWrite(int fd);

    // 广播消息
    void broadcastMessage(int senderFd, const std::string& msg);

    // 修改客户端的 epoll 监听事件
    bool modifyEpollEvents(int fd, std::uint32_t events);

    // 统一关闭客户端连接
    void closeClient(int fd);

};

#endif
