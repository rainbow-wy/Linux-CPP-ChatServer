#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cerrno>
using namespace std;

//加入epoll需要非阻塞机制,当非阻塞socket调用recv时，若无数据则返回-1
bool setNonBlocking(int fd){
    int flags = fcntl(fd,F_GETFL,0);
    if(flags == -1){
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

int main() {
    // 1. 创建 Socket
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

    //创建epoll实例
    int epollfd = epoll_create1(0);
    if(epollfd == -1){
        perror("epoll_create1");
        close(listenfd);
        return 1;
    }

    //准备监听事件
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
 
    std::cout << "Waiting for client..." << std::endl;

    epoll_event events[64];
    unordered_map<string,string> pages = {
        {"/hello","root/hello.html"},{"/test","root/test.html"}
    };

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
            cout << "events[" << i << "]"
         << " 的fd = " << fd << endl;
            //情况1：若监听socket就绪
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

                    epoll_event client_ev{};
                    client_ev.events = EPOLLIN;
                    client_ev.data.fd = connfd;

                    if(epoll_ctl(epollfd,EPOLL_CTL_ADD,connfd,&client_ev) == -1){
                        perror("epoll_ctl connfd");
                        close(connfd);
                        continue;
                    }

                    cout << "新客户端连接，fd= " << connfd << endl;
                }
                continue;
            }
            // 情况二：客户端 Socket 就绪
        char request[4096];

        ssize_t n = recv(fd, request, sizeof(request) - 1, 0);

        if (n == -1 &&
            (errno == EAGAIN || errno == EWOULDBLOCK ||
             errno == EINTR)) {
            continue;
        }

        if (n <= 0) {
            epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
            continue;
        }

        request[n] = '\0';

        cout << "收到 fd = " << fd << " 的请求:" << endl;
        cout << request << endl;

    string path;
    int left = 0;
    while(left < n && request[left] != ' '){
        left++;
    }
    if(left < n){
        for(int i = left + 1;i < n;i++){
        if(request[i] == ' '){
            break;
        }
        path += request[i];
    }
    cout << path;
    }

    //获取路径
    auto it = pages.find(path);
    
    string body;
    string status = "200 OK";
    string extra_headers;

    if(it != pages.end()){
        string filename = it->second;
        ifstream file(filename);
        if(!file.is_open()){
            cout << "failed to open file" << endl;
            status = "404 Not Found";
            body = "<h1>404 File Not Found</h1>";
        }
        else{
            string line;
            while(getline(file,line)){
                body += line + "\n";
            }
        }
    }
    else{
        status = "302 Found";
        extra_headers = "location: /hello\r\n";
    }


    string response =
    "HTTP/1.1 " + status + "\r\n"
    + extra_headers
    + "Content-Type: text/html; charset=utf-8\r\n"
    + "Content-Length: " + to_string(body.size()) + "\r\n"
    + "Connection: close\r\n"
    + "\r\n"
    + body;

    cout << "HTTP响应内容：" << endl;
    cout << response << endl;
    int iiret = send(fd,response.c_str(),response.size(),MSG_NOSIGNAL);

    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,nullptr);
    close(fd);
    }
}

    // 6. 关闭连接
    close(epollfd);
    close(listenfd);

    return 0;
}