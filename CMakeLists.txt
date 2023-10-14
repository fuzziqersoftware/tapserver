cmake_minimum_required(VERSION 3.10)



# Project setup

project(tapserver)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

set(LOCAL_INCLUDE_DIR "/usr/local/include")
set(LOCAL_LIB_DIR "/usr/local/lib")
list(APPEND CMAKE_PREFIX_PATH ${LOCAL_LIB_DIR})
include_directories(${LOCAL_INCLUDE_DIR})
link_directories(${LOCAL_LIB_DIR})



# Library and executable definitions

add_library(tapinterface MacOSNetworkTapInterface.cc)

add_executable(tapserver MacOSNetworkTapInterfaceServer.cc)
target_link_libraries(tapserver tapinterface phosg)



# Installation configuration

install(TARGETS tapinterface DESTINATION lib)
install(TARGETS tapserver DESTINATION bin)
install(FILES MacOSNetworkTapInterface.hh DESTINATION include)
