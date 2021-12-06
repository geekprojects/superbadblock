cmake_minimum_required(VERSION 3.20)
project(superbadblock)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(libgeek REQUIRED libgeek)

add_definitions(${libgeek_CFLAGS})
add_executable(superbadblock main.cpp)
target_link_libraries(superbadblock ${libgeek_LDFLAGS})
