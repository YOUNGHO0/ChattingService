cmake_minimum_required(VERSION 3.25)
project(server C)

set(CMAKE_C_STANDARD 11)

add_executable(server
        tcpServer.c)
