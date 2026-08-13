cmake_minimum_required(VERSION 3.16)

project(HttpWeb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_executable(Web main.cpp)

find_package(Threads REQUIRED)
target_link_libraries(Web PRIVATE Threads::Threads)
