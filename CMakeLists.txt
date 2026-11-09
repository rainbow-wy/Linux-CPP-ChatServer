
cmake_minimum_required(VERSION 3.16)

project(ChatServer LANGUAGES CXX)

# 使用 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 创建可执行程序
add_executable(chatserver
    src/main.cpp
    src/chat_server.cpp
    src/protocol.cpp
)

# 指定头文件目录
target_include_directories(chatserver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 开启编译警告
target_compile_options(chatserver PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
