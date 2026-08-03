cmake_minimum_required(VERSION 3.22.1)
project("shell")

file(GLOB SRCS src/main/cpp/*.cpp src/main/cpp/*.c)

add_library(security SHARED ${SRCS})

find_library(log-lib log)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED libzip)

target_link_libraries(security ${log-lib} OpenSSL::Crypto ${LIBZIP_LIBRARIES})
target_include_directories(security PRIVATE ${LIBZIP_INCLUDE_DIRS})
