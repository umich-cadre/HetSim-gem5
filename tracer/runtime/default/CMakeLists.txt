# cmake file                                                                                                         
set(PRJ_RT_NAME hetsim_default_rt)

set(SOURCES ${PRJ_RT_NAME}.cpp)

include_directories(${CMAKE_SOURCE_DIR}/../emu/inc)

add_library(${PRJ_RT_NAME} SHARED ${SOURCES})

#target_link_directories(${PRJ_RT_NAME} PUBLIC ../../../../host/fake-TM/build)
#target_link_libraries(${PRJ_RT_NAME} TMRevere)

target_link_directories(${PRJ_RT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/../emu/build)
target_link_libraries(${PRJ_RT_NAME} hetsim_prim)

#set(CMAKE_C_FLAGS -m32)                                                                                             
#set(CMAKE_CXX_FLAGS -m32)                                                                                           
#set(CMAKE_C_FLAGS "-g -m32")
#set(CMAKE_CXX_FLAGS "-g -m32")

install(
  TARGETS ${PRJ_RT_NAME}
  EXPORT ${PRJ_NAME}
  ARCHIVE DESTINATION "runtime/lib"
  LIBRARY DESTINATION "runtime/lib")
