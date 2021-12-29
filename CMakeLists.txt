cmake_minimum_required(VERSION 3.10)



# Project setup

project(phosg)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
endif()

include_directories("/usr/local/include")
link_directories("/usr/local/lib")



# Library and executable definitions

add_library(tapinterface MacOSNetworkTapInterface.cc)

add_executable(tapserver MacOSNetworkTapInterfaceServer.cc)
target_link_libraries(tapserver tapinterface phosg)



# Installation configuration

install(TARGETS tapinterface DESTINATION lib)
install(TARGETS tapserver DESTINATION bin)
install(FILES MacOSNetworkTapInterface.hh DESTINATION include)
