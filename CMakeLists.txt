cmake_minimum_required(VERSION 3.10)
project(CNA)

set(CMAKE_CXX_STANDARD 17)

#add_compile_definitions(SOUND_ENABLED)
add_compile_definitions(XNA5)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(SDL3_mixer REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_library(CNA STATIC ${SOURCES}
)

target_include_directories(CNA PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(CNA PRIVATE SDL3::SDL3 SDL3_image SDL3_mixer::SDL3_mixer)

###
add_subdirectory(vendor/googletest)

enable_testing()

file(GLOB_RECURSE TESTS "tests/*.cpp")
add_executable(Tests ${TESTS})

##target_include_directories(Tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
#target_include_directories(Tests PRIVATE
#        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/googletest/googletest/include
#        #${CMAKE_CURRENT_SOURCE_DIR}/include
#)

target_link_libraries(Tests PRIVATE CNA gtest_main)

target_include_directories(Tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/googletest/googletest/include)

include(GoogleTest)
gtest_discover_tests(Tests)

