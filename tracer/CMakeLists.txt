cmake_minimum_required(VERSION 3.7)
project(memtest)

cmake_policy(SET CMP0077 NEW)

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1 -g")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -g")

set(CMAKE_CXX_STANDARD 14)

find_package(LLVM 10.0.0 REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(HandleLLVMOptions)
include(AddLLVM)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
#include_directories("../../include")
#include_directories("../../code/include")

add_subdirectory(compiler-pass)
add_subdirectory(runtime)
