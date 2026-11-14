cmake_minimum_required(VERSION 3.21)

project(cooling_telemetry_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(cooling_telemetry_gui WIN32
    main.cpp
    mainwindow.cpp
    telemetryplot.cpp
)

target_include_directories(cooling_telemetry_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(cooling_telemetry_gui PRIVATE ctm_core Qt6::Widgets)

if(MSVC)
    target_compile_options(cooling_telemetry_gui PRIVATE /W4 /utf-8)
else()
    target_compile_options(cooling_telemetry_gui PRIVATE -Wall -Wextra -pthread)
    target_link_options(cooling_telemetry_gui PRIVATE -pthread)
endif()
