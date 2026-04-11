cmake_minimum_required(VERSION 3.20)

project(CNA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(SDL3_mixer REQUIRED)
find_package(SDL3_ttf REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        "src/*.cpp"
)

add_library(CNA STATIC
        ${SOURCES}
)

target_include_directories(CNA
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(CNA
        PUBLIC
        SOUND_ENABLED
        XNA5
)

target_link_libraries(CNA
        PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_mixer::SDL3_mixer
        SDL3_ttf::SDL3_ttf
)

add_subdirectory(vendor/googletest)

enable_testing()

file(GLOB_RECURSE TESTS CONFIGURE_DEPENDS
        "tests/*.cpp"
)

add_executable(Tests
        ${TESTS}
)

target_include_directories(Tests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/googletest/googletest/include
)

target_link_libraries(Tests
        PRIVATE
        CNA
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(Tests)