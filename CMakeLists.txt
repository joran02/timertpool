
cmake_minimum_required(VERSION 2.8)
PROJECT(MAIN)
SET(CMAKE_SYSTEM_NAME linux)

file(GLOB SRC_LIST "*.c" )

ADD_EXECUTABLE(main ${SRC_LIST})
target_link_libraries(main  pthread)
