cmake_minimum_required(VERSION 3.10)
project(socks5-proxy LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_executable(server server.cpp)

if(WIN32)
    target_link_libraries(server ws2_32 winhttp)
endif()
