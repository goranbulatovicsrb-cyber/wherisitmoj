cmake_minimum_required(VERSION 3.16)
project(WhereIsByLabel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(whereisbylabel
    src/main.cpp
)

if (WIN32)
  target_compile_definitions(whereisbylabel PRIVATE UNICODE _UNICODE)
endif()
