
#include "chat_server.h"

#include <iostream>

int main() {

    // 创建服务器对象，监听 9007 端口
    ChatServer server(9007);

    // 初始化服务器
    if (!server.init()) {
        std::cerr << "服务器初始化失败" << std::endl;
        return 1;
    }

    // 启动服务器事件循环
    server.run();

    return 0;
}
