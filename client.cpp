#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
using namespace std;

int main(){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9006);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int ret = connect(sockfd, (struct sockaddr*)& address,sizeof(address));

    if(ret == -1){
        perror("connect");
        close(sockfd);
        return 1;
    }


    char buffer[1024];
    for(int ii = 0;ii < 3;ii++){
        int iret;
        memset(buffer,0,sizeof(buffer));
        sprintf(buffer,"这是第%d个，编号%03d。",ii + 1,ii + 1);
        sleep(10);
        if((iret = send(sockfd,buffer,strlen(buffer),0)) <= 0){
            perror("send");
            break;
        }
    std::cout<<"发送："<< buffer <<std::endl;

    memset(buffer,0,sizeof(buffer));
    //接收服务器报文
    if((iret = recv(sockfd,buffer,sizeof(buffer),0)) <= 0){
        cout << "iret =" << iret << endl;
        break;
    }
    std::cout <<"ok："<<buffer<< endl;
    sleep(1);
}
close(sockfd);
return 0;
}
    