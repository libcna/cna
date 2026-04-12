cmake_minimum_required(VERSION 3.20)

project(CNA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CNA_BUILD_TESTS "Build CNA tests" ON)

set(CPP_DOTNET_BUILD_TESTS OFF CACHE BOOL "Build cpp-dotnet tests" FORCE)
add_subdirectory(../cpp-dotnet CPP_DOTNET)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(SDL3_mixer REQUIRED)
find_package(SDL3_ttf REQUIRED)

file(GLOB_RECURSE CNA_SOURCES CONFIGURE_DEPENDS
        "src/*.cpp"
)

add_library(CNA STATIC
        ${CNA_SOURCES}
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
        PUBLIC
        CPP_DOTNET
        PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_mixer::SDL3_mixer
        SDL3_ttf::SDL3_ttf
)

if(CNA_BUILD_TESTS)
    add_subdirectory(vendor/googletest)

    enable_testing()

    file(GLOB_RECURSE CNA_TEST_SOURCES CONFIGURE_DEPENDS
            "tests/*.cpp"
    )

    add_executable(CnaTests
            ${CNA_TEST_SOURCES}
    )

    target_link_libraries(CnaTests
            PRIVATE
            CNA
            CPP_DOTNET
            gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(CnaTests)
endif()