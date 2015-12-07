cmake_minimum_required(VERSION 3.3)
project(shell_js)

include_directories(v7 mongoose)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
add_definitions(-DV7_BUILD_PROFILE=3 -DV7_ENABLE__Memory__stats -DV7_ENABLE_COMPACTING_GC -DV7_ENABLE_FILE -DMG_ENABLE_THREADS -DMG_USE_READ_WRITE)


add_library(mongoose mongoose/mongoose.c)
set_property(TARGET mongoose PROPERTY COMPILE_FLAGS "-DEXCLUDE_COMMON")

add_library(v7 v7/v7.c)

add_executable(jssh main.c)

target_link_libraries(jssh v7 mongoose m)

INSTALL_TARGETS(/bin jssh)