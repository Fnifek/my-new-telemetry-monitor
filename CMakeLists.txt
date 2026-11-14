cmake_minimum_required(VERSION 3.16)

project(cooling_telemetry_monitor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ctm_core
    src/model_sensor.cpp
    src/sampler.cpp
    src/filters.cpp
    src/acquisition_pipeline.cpp
    src/csv_writer.cpp
    src/platform.cpp
)

target_include_directories(ctm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
target_link_libraries(ctm_core PUBLIC Threads::Threads)

if(WIN32)
    # timeBeginPeriod/timeEndPeriod живут в winmm.
    target_link_libraries(ctm_core PUBLIC winmm)
endif()

if(MSVC)
    target_compile_options(ctm_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(ctm_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ctm_acq src/main.cpp)
target_link_libraries(ctm_acq PRIVATE ctm_core)

add_executable(ctm_tests tests/test_main.cpp)
target_link_libraries(ctm_tests PRIVATE ctm_core)

enable_testing()
add_test(NAME ctm_tests COMMAND ctm_tests)

# Qt GUI собирается только если найден Qt6 Widgets. Ядро и CLI от Qt не зависят.
option(CTM_BUILD_GUI "Build Qt Widgets GUI when Qt6 is available" ON)
if(CTM_BUILD_GUI)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        add_subdirectory(gui)
    else()
        message(STATUS "Qt6 Widgets not found: GUI target skipped")
    endif()
endif()

# ЛР3 использует Windows CNG (bcrypt), поэтому только на Windows.
option(CTM_BUILD_LAB3 "Build Lab 3 hardware model (Windows only)" ON)
if(CTM_BUILD_LAB3 AND WIN32)
    add_subdirectory(lab3/hw_model)
endif()
