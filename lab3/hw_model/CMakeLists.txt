cmake_minimum_required(VERSION 3.16)

project(ctm_hw_model VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "The Lab 3 model uses Windows CNG (bcrypt) for AES-256-GCM")
endif()

add_executable(ctm_hw ctm_hw.cpp main.cpp)
target_include_directories(ctm_hw PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ctm_hw PRIVATE bcrypt)

if(MSVC)
    target_compile_options(ctm_hw PRIVATE /W4 /utf-8)
else()
    target_compile_options(ctm_hw PRIVATE -Wall -Wextra)
endif()
