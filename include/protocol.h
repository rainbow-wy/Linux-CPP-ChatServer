#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>

namespace Protocol {

    // 从缓冲区中提取一条完整消息
    bool extractMessage(std::string& buffer,std::string& message);

}

#endif