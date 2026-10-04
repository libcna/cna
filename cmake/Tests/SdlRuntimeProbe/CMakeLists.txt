cmake_minimum_required(VERSION 3.20)

project(CNASdlRuntimeProbe LANGUAGES CXX)

foreach(_required IN ITEMS THIRD_PARTY_SDL RUNTIME_DIRECTORY)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

set(WIN32 TRUE)
set(CNA_SDL_RUNTIME_LIBRARIES
    "${RUNTIME_DIRECTORY}/SDL3.dll;${RUNTIME_DIRECTORY}/SDL3_image.dll;${RUNTIME_DIRECTORY}/SDL3_mixer.dll"
    CACHE INTERNAL "" FORCE)
include("${THIRD_PARTY_SDL}")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
add_executable(sdl_runtime_consumer "${CMAKE_CURRENT_BINARY_DIR}/main.cpp")
cna_copy_sdl_runtime(sdl_runtime_consumer)
