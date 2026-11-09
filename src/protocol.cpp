#include "protocol.h"

namespace Protocol {

    bool extractMessage(std::string& buffer,std::string& message) {

        size_t pos = buffer.find('\n');

        if(pos == std::string::npos) {
            return false;
        }

        // 提取完整消息，不包含'\n'
        message = buffer.substr(0, pos);

        //从接收缓冲区中删除已经处理的数据
        buffer.erase(0, pos + 1);

        return true;
    }

}