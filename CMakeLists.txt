cmake_minimum_required(VERSION 3.15) 
project(shared_ptr_benchmark)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


include(FetchContent)
FetchContent_Declare(
  google_benchmark
  GIT_REPOSITORY https://github.com/google/benchmark
  GIT_TAG        v1.9.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(google_benchmark)


add_library(benchmark_config INTERFACE)

target_link_libraries(benchmark_config INTERFACE 
    benchmark::benchmark 
    benchmark::benchmark_main
)

find_package(Threads REQUIRED)
target_link_libraries(benchmark_config INTERFACE Threads::Threads)


if(MSVC)
    target_compile_options(benchmark_config INTERFACE /O2 /GS-)
else()

    target_compile_options(benchmark_config INTERFACE -O3)
    
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(benchmark_config INTERFACE -march=native)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(benchmark_config INTERFACE rt)
    endif()
endif()


set(BENCHMARK_TARGETS
    "shared_state_benchmark"        "baseline.cpp"
    "atomic_shared_state_benchmark" "atomic_shared.cpp"
    "vector_and_atomic_state_benchmark" "vector_and_atomic.cpp"
    "index_rcu_state_benchmark" "index_rcu.cpp"
    "cppurcu_git_state_benchmark" "cppurcu_git.cpp"
    "epoch_rcu_state_benchmark" "epoch_rcu.cpp"
    "test_state_benchmark" "test.cpp"
    "buffer_hazard_state_benchmark" "buffer_hazard.cpp"
)



while(BENCHMARK_TARGETS)
    list(POP_FRONT BENCHMARK_TARGETS TARGET_NAME SRC_FILE)
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SRC_FILE}")
        add_executable(${TARGET_NAME} ${SRC_FILE})
        target_link_libraries(${TARGET_NAME} PRIVATE benchmark_config)
    endif()
endwhile()

target_include_directories(cppurcu_git_state_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(TARGET benchmark)
    if(MSVC)
        target_compile_options(benchmark PRIVATE /W0 /WX-)
    else()
        target_compile_options(benchmark PRIVATE -w)
    endif()
endif()