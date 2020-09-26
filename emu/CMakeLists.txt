cmake_minimum_required(VERSION 3.0)
project(HetSim-primlib)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -MMD -MP -O2 -g")

file(GLOB SOURCES "src/*.cpp")

include_directories(inc)

add_library(hetsim_prim SHARED ${SOURCES})
install(TARGETS hetsim_prim DESTINATION build)

MESSAGE(STATUS "Generated hetsim_prim library")
