#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
using namespace std;

int main(){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1){
        perror("socket");
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9007);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int ret = connect(sockfd, (struct sockaddr*)& address,sizeof(address));

    if(ret == -1){
        perror("connect");
        close(sockfd);
        return 1;
    }

    send(sockfd,"hel",3,0);
    sleep(1);
    send(sockfd,"lo\n",3,0);
    close(sockfd);
    return 0;
    }