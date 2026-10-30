cmake_minimum_required(VERSION 3.20)

project(CNASdlRuntimeProbe LANGUAGES CXX)

foreach(_required IN ITEMS THIRD_PARTY_SDL RENDERER_RUNTIME DEPLOY_RUNTIME RUNTIME_DIRECTORY)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

set(WIN32 TRUE)
include("${DEPLOY_RUNTIME}")

# Installed-package route: only imported dependency targets and the exported central helper exist.
add_library(SDL3::SDL3-shared SHARED IMPORTED)
set_target_properties(SDL3::SDL3-shared PROPERTIES
    IMPORTED_LOCATION "${RUNTIME_DIRECTORY}/SDL3.dll")
add_library(SDL3_mixer::SDL3_mixer-shared SHARED IMPORTED)
set_target_properties(SDL3_mixer::SDL3_mixer-shared PROPERTIES
    IMPORTED_LOCATION "${RUNTIME_DIRECTORY}/SDL3_mixer.dll")
add_library(WebGPU::WebGPU SHARED IMPORTED)
set_target_properties(WebGPU::WebGPU PROPERTIES
    IMPORTED_LOCATION "${RUNTIME_DIRECTORY}/wgpu_native.dll")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/main.cpp" "int main() { return 0; }\n")
add_executable(installed_runtime_consumer "${CMAKE_CURRENT_BINARY_DIR}/main.cpp")
set_target_properties(installed_runtime_consumer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/installed")
cna_deploy_runtime(installed_runtime_consumer)
cna_deploy_runtime(installed_runtime_consumer)

# Source-tree route: the legacy focused helpers remain available and the central helper composes
# them, preserving existing explicit runtime paths as applications migrate to one call.
set(CNA_SDL_RUNTIME_LIBRARIES
    "${RUNTIME_DIRECTORY}/SDL3.dll;${RUNTIME_DIRECTORY}/SDL3_mixer.dll"
    CACHE INTERNAL "" FORCE)
set(CNA_WEBGPU_RUNTIME_LIBRARY "${RUNTIME_DIRECTORY}/wgpu_native.dll" CACHE INTERNAL "" FORCE)
include("${THIRD_PARTY_SDL}")
include("${RENDERER_RUNTIME}")

add_executable(sdl_runtime_consumer "${CMAKE_CURRENT_BINARY_DIR}/main.cpp")
set_target_properties(sdl_runtime_consumer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/source")
cna_deploy_runtime(sdl_runtime_consumer)
cna_deploy_runtime(sdl_runtime_consumer)
