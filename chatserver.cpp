#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cerrno>
using namespace std;

bool setNonBlocking(int fd){
    int flags = fcntl(fd,F_GETFL,0);
    if(flags == -1){
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

int main(){
    // 1. 创建socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    if (listenfd == -1) {
        perror("socket");
        return 1;
    }

    // 2. 准备服务器的 IP 和端口信息
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9007);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // 3. 将 Socket 与本地地址绑定
    if (bind(listenfd, (sockaddr*)&address, sizeof(address)) == -1) {
        perror("bind");
        close(listenfd);
        return 1;
    }

    // 4. 开始监听
    if (listen(listenfd, 5) == -1) {
        perror("listen");
        close(listenfd);
        return 1;
    }

    //将监听socket 设置为非阻塞
    if(!setNonBlocking(listenfd)){
        perror("setNonBlocking");
        close(listenfd);
        return 1;
    }

    int epollfd = epoll_create1(0);
    if(epollfd == -1){
        perror("epoll_create1");
        close(listenfd);
        return 1;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listenfd;

    //将listenfd注册到epoll
    if(epoll_ctl(epollfd,EPOLL_CTL_ADD,listenfd,&ev) == -1){
        perror("epoll_ctl");
        close(epollfd);
        close(listenfd);
        return 1;
    }

    cout << "Waiting for client..." << endl;

    unordered_set<int> clients;
    unordered_map<int,string> recvBuffers;
    unordered_map<int,string> sendBuffers;

    epoll_event events[64];

    while(1){
        int nready = epoll_wait(epollfd,events,64,-1);

        if(nready == -1){
            if(errno == EINTR){
                continue;
            }
            perror("epoll_wait");
            break;
        }
        cout << "本次事件就绪数量："<<nready << endl;

        for(int i = 0;i < nready;i++){
            int fd = events[i].data.fd;

            //情况1：若有新连接就绪
            if(fd == listenfd){
                while(1){
                    int connfd = accept(listenfd,nullptr,nullptr);
                    if(connfd == -1){
                        if(errno == EAGAIN || errno == EWOULDBLOCK){
                            break;
                        }
                        if(errno == EINTR){
                            continue;
                        }
                        perror("accept");
                        break;
                    }
                    if(!setNonBlocking(connfd)){
                        perror("setNonBlocking connfd");
                        close(connfd);
                        continue;
                    }

                    //创建connfd事件
                    epoll_event client_ev{};
                    client_ev.events = EPOLLIN;
                    client_ev.data.fd = connfd;

                    if(epoll_ctl(epollfd,EPOLL_CTL_ADD,connfd,&client_ev) == -1){
                        perror("epoll_ctl");
                        close(connfd);
                        continue;
                    }
                    clients.insert(connfd);

                    cout << "新客户端连接：fd = " << connfd << endl;
                    cout << "当前在线客户端数量：" << clients.size() << endl;

                }
                continue;
            }

            uint32_t revents = events[i].events;

            if(revents & EPOLLIN){
                char buffer[4096];
                ssize_t n = recv(fd,buffer,sizeof(buffer) - 1,0);
                if(n > 0){

                    cout << "本次recv收到 " << n << " 个字节：";
                    cout.write(buffer, n);
                    cout << endl;
                    //规定消息以\n结尾
                    recvBuffers[fd].append(buffer,n);
                    size_t pos;
                    while((pos = recvBuffers[fd].find('\n')) != string::npos){
                        string msg = recvBuffers[fd].substr(0,pos);
                        cout <<  "收到fd = " << fd << "的消息：" << msg <<endl;
                        recvBuffers[fd].erase(0,pos + 1);
                        
                        string sendmsg = msg + '\n';
                        for(auto it = clients.begin();it != clients.end();){
                            int clientfd = *it;
                            if(clientfd == fd){
                                ++it;
                                continue;
                            }
                        
                            //先检查缓冲区是否为空
                            bool wasEmpty = sendBuffers[clientfd].empty();

                            //将消息加入待发送缓冲区、
                            sendBuffers[clientfd] += sendmsg;

                            //原本为空，说明刚刚产生了待发送任务
                            if(wasEmpty){
                                epoll_event ev{};
                                ev.events = EPOLLIN | EPOLLOUT;
                                ev.data.fd = clientfd;
                                if(epoll_ctl(epollfd,EPOLL_CTL_MOD,clientfd,&ev) == -1){
                                    int err = errno;
                                    cerr << "为客户端 fd = "<< clientfd <<"开启EPOLLOUT失败" << strerror(err) << " (errno=" << err << ")" << endl;

                                    it = clients.erase(it);
                                    recvBuffers.erase(clientfd);
                                    sendBuffers.erase(clientfd);
                                    epoll_ctl(epollfd,EPOLL_CTL_DEL,clientfd,nullptr);
                                    close(clientfd);
                                    continue;
                                }
                                cout << "[开启EPOLLOUT] fd = " << clientfd << "，待发送字节数 = " << sendBuffers[clientfd].size() << endl;
                            }
                            ++it;
                        }
                    }
               }

               else if(n == 0){
                clients.erase(fd);
                recvBuffers.erase(fd);
                sendBuffers.erase(fd);
                cout << "客户端断开，fd = " << fd << endl;
                cout << "当前在线连接数：" << clients.size() << endl;
                epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
                continue;
               }

               else{
                if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR){
                    //暂时无数据，或系统调用被信号中断
                    //不关闭连接，也不跳过可能同时存在的EPOLLOUT
                }
                else{
                    perror("recv");
                    clients.erase(fd);
                    recvBuffers.erase(fd);
                    sendBuffers.erase(fd);
                    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,nullptr);
                    close(fd);
                    continue;
                }
               }
            }

            if(revents & EPOLLOUT){
                string& pending = sendBuffers[fd];
                cout << "[EPOLLOUT就绪] fd = " << fd << "，待发送字节数 = " << pending.size() << endl;
                bool sendError = false;
                while(!pending.empty()){
                    ssize_t sent = send(fd,pending.data(),pending.size(),MSG_NOSIGNAL);
                    if(sent > 0){
                        cout << "[send成功] fd = " << fd << "，本次发送 = " << sent << " 字节" << endl;
                        pending.erase(0,sent);
                        cout << "[缓冲区更新] fd = " << fd << "，剩余待发送 = " << pending.size() << " 字节" << endl;
                    }
                    else if(sent == -1 && errno == EINTR){
                        continue;
                    }
                    else if(sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)){
                        cout << "[send遇到EAGAIN] fd = " << fd << "，剩余待发送 = " << pending.size() << " 字节" << endl;
                        break;
                    }
                    else{
                        //其他发送失败，或者send返回0且数据仍未发完
                        if(sent == -1){
                            perror("send");
                        }
                        sendError = true;
                        break;
                    }
                }
                //发生不可恢复的错误，关闭连接
                if(sendError){
                    clients.erase(fd);
                    recvBuffers.erase(fd);
                    sendBuffers.erase(fd);
                    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,nullptr);
                    close(fd);
                    continue;
                }

                //数据全部发送完成
                if(pending.empty()){
                    epoll_event ev{};
                    ev.events = EPOLLIN;
                    ev.data.fd = fd;

                    if(epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&ev) == -1){
                        perror("epoll_ctl MOD");
                        clients.erase(fd);
                        recvBuffers.erase(fd);
                        sendBuffers.erase(fd);
                        epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,nullptr);
                        close(fd);
                        continue;
                    }
                    cout << "[关闭EPOLLOUT] fd = " << fd << "，发送缓冲区已清空" << endl;
                }
            }
        }
    }
}