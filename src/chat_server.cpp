
#include "chat_server.h"
#include "protocol.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cerrno>
#include <iostream>
#include <sys/epoll.h>

// ================================
// 1. 辅助函数（创建socket非阻塞状态）
// ================================

namespace {

bool setNonBlocking(int fd) {

    int flags = fcntl(fd, F_GETFL, 0);

    if (flags == -1) {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

}


// ================================
// 2. 构造函数和析构函数
// ================================

ChatServer::ChatServer(int port)
    : port_(port) {
}

ChatServer::~ChatServer() {

    // 关闭所有客户端连接
    for (auto& client : clients_) {
        close(client.first);
    }

    clients_.clear();

    // 关闭监听 Socket
    if (listenFd_ != -1) {
        close(listenFd_);
        listenFd_ = -1;
    }

    // 关闭 epoll
    if (epollFd_ != -1) {
        close(epollFd_);
        epollFd_ = -1;
    }

}


// ================================
// 3. 初始化服务器
// ================================

bool ChatServer::init() {

    // 创建 Socket
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);

    if (listenFd_ == -1) {
        perror("socket");
        return false;
    }

    // 准备服务器地址
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // 绑定地址
    if (bind(listenFd_,(sockaddr*)&address,sizeof(address)) == -1) {
        perror("bind");
        return false;
    }

    // 开始监听
    if (listen(listenFd_, 5) == -1) {
        perror("listen");
        return false;
    }

    // 设置监听 Socket 为非阻塞
    if (!setNonBlocking(listenFd_)) {
        perror("setNonBlocking listenfd");
        return false;
    }

    // 创建 epoll
    epollFd_ = epoll_create1(0);

    if (epollFd_ == -1) {
        perror("epoll_create1");
        return false;
    }

    // 注册监听 Socket
    epoll_event ev{};

    ev.events = EPOLLIN;
    ev.data.fd = listenFd_;

    if (epoll_ctl(epollFd_,EPOLL_CTL_ADD,listenFd_,&ev) == -1) {
        perror("epoll_ctl ADD listenfd");
        return false;
    }

    std::cout << "服务器初始化成功，端口：" << port_ << std::endl;

    return true;
}


// ================================
// 4. 服务器主事件循环
// ================================

void ChatServer::run() {

    if (listenFd_ == -1 || epollFd_ == -1) {
        std::cerr << "服务器尚未初始化" << std::endl;
        return;
    }

    std::cout << "Waiting for client..." << std::endl;

    epoll_event events[MAX_EVENTS];

    while (true) {
        //返回就绪事件数
        int nready = epoll_wait(epollFd_,events,MAX_EVENTS,-1);

        if (nready == -1) {

            if (errno == EINTR) {
                continue;
            }

            perror("epoll_wait");
            break;
        }

        std::cout << "本次事件就绪数量：" << nready << std::endl;

        for (int i = 0; i < nready; i++) {

            int fd = events[i].data.fd;

            // 监听 Socket 有事件：接受新连接
            if (fd == listenFd_) {
                acceptClients();
                continue;
            }

            // 处理客户端读写事件
            handleClientEvent(fd, events[i].events);
        }
    }
}


// ================================
// 5. 接受客户端连接
// ================================

void ChatServer::acceptClients() {

    while (true) {
        //接收连接
        int connFd = accept(listenFd_, nullptr, nullptr);

        if (connFd == -1) {

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            perror("accept");
            break;
        }

        // 设置客户端 Socket 为非阻塞
        if (!setNonBlocking(connFd)) {
            perror("setNonBlocking connfd");
            close(connFd);
            continue;
        }

        // 注册客户端事件
        epoll_event clientEv{};
        clientEv.events = EPOLLIN | EPOLLRDHUP;//关注可读事件和对端关闭发送的事件
        clientEv.data.fd = connFd;

        //将connfd注册到epoll
        if (epoll_ctl(epollFd_,EPOLL_CTL_ADD,connFd,&clientEv) == -1) {
            perror("epoll_ctl ADD client");
            close(connFd);
            continue;
        }

        // 创建客户端状态
        clients_.emplace(connFd, ClientSession{});

        std::cout << "新客户端连接：fd = "<< connFd << std::endl;
        std::cout << "当前在线客户端数量："<< clients_.size() << std::endl;
    }
}


// ================================
// 6. 处理客户端事件
// ================================

void ChatServer::handleClientEvent(int fd,std::uint32_t revents) {

    // 处理可读事件
    if (revents & EPOLLIN) {
        handleRead(fd);
    }

    // handleRead 可能已经关闭连接
    // 因此需要再次检查客户端是否存在
    if (clients_.find(fd) == clients_.end()) {
        return;
    }

    // 处理可写事件
    if (revents & EPOLLOUT) {
        handleWrite(fd);
    }
}


// ================================
// 7. 接收客户端消息
// ================================

void ChatServer::handleRead(int fd) {

    auto it = clients_.find(fd);

    if (it == clients_.end()) {
        return;
    }

    char buffer[4096];

    ssize_t n;

    do {
        n = recv(fd, buffer, sizeof(buffer), 0);
    } while (n == -1 && errno == EINTR);

    if (n > 0) {

        std::cout << "本次recv收到 "<< n << " 个字节：";
        std::cout.write(buffer, n);
        std::cout << std::endl;

        // 追加到该客户端的接收缓冲区
        it->second.recvBuffer.append(buffer, n);

        std::string msg;

        // 不断提取完整消息
        while (Protocol::extractMessage(
            it->second.recvBuffer, msg)) {
            std::cout << "收到fd = "<< fd<< "的消息："<< msg<< std::endl;
            broadcastMessage(fd, msg);
        }

    }

    else if (n == 0) {

        std::cout << "客户端断开，fd = "<< fd << std::endl;
        closeClient(fd);
    }

    else {

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }

        perror("recv");

        closeClient(fd);
    }
}


// ================================
// 8. 广播消息
// ================================

void ChatServer::broadcastMessage(int senderFd,const std::string& msg) {

    std::string sendMsg = msg + '\n';

    for (auto it = clients_.begin();it != clients_.end();) {

        int clientFd = it->first;

        // 不向消息发送者自己转发
        if (clientFd == senderFd) {
            ++it;
            continue;
        }

        // 检查发送缓冲区是否为空
        bool wasEmpty = it->second.sendBuffer.empty();

        // 添加待发送消息
        it->second.sendBuffer += sendMsg;

        // 原来为空，现在有待发送数据
        if (wasEmpty) {

            if (!modifyEpollEvents(clientFd,EPOLLIN | EPOLLOUT)) {
                int failedFd = clientFd;
                ++it;
                std::cerr << "开启EPOLLOUT失败，fd = "<< failedFd << std::endl;
                closeClient(failedFd);
                continue;
            }
            std::cout << "[开启EPOLLOUT] fd = "<< clientFd<< "，待发送字节数 = "<< clients_.at(clientFd).sendBuffer.size()<< std::endl;
        }

        ++it;
    }
}


// ================================
// 9. 发送客户端消息
// ================================

void ChatServer::handleWrite(int fd) {

    auto it = clients_.find(fd);

    if (it == clients_.end()) {
        return;
    }

    std::string& pending = it->second.sendBuffer;

    std::cout << "[EPOLLOUT就绪] fd = "<< fd<< "，待发送字节数 = "<< pending.size()<< std::endl;

    bool sendError = false;

    while (!pending.empty()) {

        ssize_t sent = send(fd,pending.data(),pending.size(),MSG_NOSIGNAL);

        if (sent > 0) {

            std::cout << "[send成功] fd = "<< fd<< "，本次发送 = "<< sent << " 字节"<< std::endl;

            pending.erase(0, static_cast<size_t>(sent));

            std::cout << "[缓冲区更新] fd = "<< fd<< "，剩余待发送 = "<< pending.size()<< " 字节"<< std::endl;
        }

        else if (sent == -1 && errno == EINTR) {
            continue;
        }

        else if (sent == -1 &&
                 (errno == EAGAIN || errno == EWOULDBLOCK)) {

            std::cout << "[send遇到EAGAIN] fd = "<< fd<< "，剩余待发送 = "<< pending.size()<< " 字节"<< std::endl;
            break;
        }

        else {

            if (sent == -1) {
                perror("send");
            }

            sendError = true;
            break;
        }
    }

    // 发送发生不可恢复的错误
    if (sendError) {
        closeClient(fd);
        return;
    }

    // 所有数据已经发送完成
    if (pending.empty()) {

        if (!modifyEpollEvents(fd, EPOLLIN)) {
            closeClient(fd);
            return;
        }

        std::cout << "[关闭EPOLLOUT] fd = "<< fd<< "，发送缓冲区已清空"<< std::endl;
    }
}


// ================================
// 10. 修改 epoll 监听事件
// ================================

bool ChatServer::modifyEpollEvents(int fd,std::uint32_t events) {

    epoll_event ev{};

    ev.events = events | EPOLLRDHUP;
    ev.data.fd = fd;

    if (epoll_ctl(epollFd_,EPOLL_CTL_MOD,fd, &ev) == -1) {
        perror("epoll_ctl MOD");
        return false;
    }
    return true;
}


// ================================
// 11. 统一关闭客户端
// ================================

void ChatServer::closeClient(int fd) {

    auto it = clients_.find(fd);

    if (it == clients_.end()) {
        return;
    }

    // 从 epoll 中删除
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);

    // 关闭 Socket
    close(fd);

    // 删除客户端状态
    clients_.erase(it);

    std::cout << "客户端 fd = "<< fd<< " 已清理"<< std::endl;

    std::cout << "当前在线客户端数量："<< clients_.size()<< std::endl;
}
