cmake_minimum_required(VERSION 3.10)
project(CNA)

set(CMAKE_CXX_STANDARD 17)

#add_compile_definitions(SOUND_ENABLED)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(SDL3_mixer REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_library(CNA STATIC ${SOURCES}
        src/Microsoft/Xna/Framework/Graphics/Viewport.cpp
)

target_include_directories(CNA PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(CNA PRIVATE SDL3::SDL3 SDL3_image SDL3_mixer::SDL3_mixer)
