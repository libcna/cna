cmake_minimum_required(VERSION 3.10)
project(CNA)

set(CMAKE_CXX_STANDARD 17)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)

add_subdirectory(../neo-sdk NEO_SDK)
include_directories(neo-sdk/include NEO_SDK)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_library(CNA STATIC ${SOURCES})

target_include_directories(CNA PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(CNA PRIVATE NEO_SDK SDL3::SDL3 SDL3_image)
