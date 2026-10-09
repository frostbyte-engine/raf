cmake_minimum_required(VERSION 3.15)
project(raf LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE RAF_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE RAF_HEADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.h")

add_executable(raf ${RAF_SOURCES} ${RAF_HEADERS})

target_include_directories(raf PRIVATE "${CMAKE_SOURCE_DIR}/src")

find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

target_link_libraries(raf PRIVATE CURL::libcurl ZLIB::ZLIB)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(raf PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(raf PRIVATE /W4)
endif()
