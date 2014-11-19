cmake_minimum_required(VERSION 2.8.4)
project(nxcreole)

# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES main.c nxcreole_parser.c)
add_executable(nxcreole ${SOURCE_FILES})