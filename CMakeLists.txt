cmake_minimum_required(VERSION 3.5)
project(ThreadPoolAction LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
enable_testing()

add_subdirectory(LogSystem)
add_subdirectory(threadpool)
add_subdirectory(chatsystem)
add_subdirectory(tests)
