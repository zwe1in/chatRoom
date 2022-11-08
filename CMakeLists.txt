cmake_minimum_required(VERSION 3.10.0)

project(chatRoom)

set(CMAKE_CXX_STANDARD 11)

add_executable(client client.cpp)
add_executable(server server.cpp)

target_link_libraries(client pthread)
target_link_libraries(server pthread)