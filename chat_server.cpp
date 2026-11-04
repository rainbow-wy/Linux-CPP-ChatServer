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

                    cout << "新客户端连接：" << endl;

                }
                continue;
            }

            char buffer[4096];

            ssize_t n = recv(fd,buffer,sizeof(buffer) - 1,0);
            if (n == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK ||
             errno == EINTR)) {
            continue;
            }

            if (n <= 0) {
                clients.erase(fd);
                recvBuffers.erase(fd);
                if(n == 0){
                    cout << "客户端断开，fd = " << fd << endl;
                    cout << "当前在线连接数：" << clients.size() << endl;
                }
                else{
                    perror("recv");
                }

                epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,nullptr);
                close(fd);
                continue;
            }

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

                for(int clientfd : clients){
                    if(clientfd == fd){
                        continue;
                    }
                    string sendmsg = msg + '\n';
                    ssize_t sent = send(clientfd,sendmsg.c_str(),sendmsg.size(),MSG_NOSIGNAL);
                    cout << "消息转发：" << fd
                 << " -> " << clientfd
                  << "，发送字节数：" << sent << endl;
                }
            }
            

        }
    }


}