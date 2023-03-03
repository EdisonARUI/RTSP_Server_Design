cmake_minimum_required(VERSION 3.22)
project(Project)

set(CMAKE_CXX_STANDARD 14)
link_libraries(ws2_32)
add_executable(Project main.cpp socket_tcp_server.cpp socket_tcp_server.h)
