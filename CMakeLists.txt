cmake_minimum_required(VERSION 3.30)
project(Filesystem C)

set(CMAKE_C_STANDARD 23)

add_executable(Filesystem main.c
        system_structures.h)
