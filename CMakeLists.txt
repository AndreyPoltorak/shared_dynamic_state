cmake_minimum_required(VERSION 3.14)
project(shared_ptr_benchmark)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(benchmark REQUIRED)

add_executable(shared_state_benchmark baseline.cpp)

target_link_libraries(shared_state_benchmark
    benchmark::benchmark
    benchmark::benchmark_main
)

target_compile_options(shared_state_benchmark PRIVATE
    $<$<CONFIG:RELEASE>:-O3>
    $<$<CONFIG:RELWITHDEBINFO>:-O3 -g>
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(shared_state_benchmark PRIVATE
        -lrt
        -pthread
    )
endif()
