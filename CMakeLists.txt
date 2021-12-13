cmake_minimum_required(VERSION 3.20)
project(superbadblock)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(libgeek REQUIRED libgeek)
pkg_check_modules(fuse REQUIRED fuse)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_definitions(${libgeek_CFLAGS} ${fuse_CFLAGS})

add_library(libsbb SHARED
        include/utils.h
        include/scanner.h
        src/libsbb/utils.cpp
        src/libsbb/apfsscanner.cpp
        include/apfsscanner.h
        src/libsbb/filesystem.cpp
        include/filesystem.h
        src/libsbb/diskscanner.cpp
        include/diskscanner.h)
target_link_libraries(libsbb ${libgeek_LDFLAGS})

add_executable(superbadblock src/filehunter/main.cpp)
target_link_libraries(superbadblock libsbb)

add_executable(sbbfuse src/sbbfuse/main.cpp)
target_link_libraries(sbbfuse libsbb ${fuse_LDFLAGS})
