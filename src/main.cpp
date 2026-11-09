#include "chat_server.h"
#include <iostream>

int main() {

    ChatServer server(9007);

    if (!server.init()) {
        std::cerr << "服务器初始化失败" << std::endl;
        return 1;
    }

    server.run();

    return 0;
}