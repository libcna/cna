cmake_minimum_required(VERSION 3.20)

project(CNAWebGPUDiscoveryProbe NONE)

foreach(_required IN ITEMS THIRDPARTY_WEBGPU PACKAGE_ROOT UNRELATED_SYSROOT)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

# Reproduce a cross-toolchain policy that permits headers and libraries only
# below its sysroot. CNA_WEBGPU_ROOT is an explicit package path outside it.
set(CMAKE_FIND_ROOT_PATH "${UNRELATED_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(WIN32 TRUE)
set(MINGW TRUE)
set(CNA_WEBGPU_ROOT "${PACKAGE_ROOT}" CACHE PATH "" FORCE)
set(CNA_WEBGPU_AUTO_DOWNLOAD OFF CACHE BOOL "" FORCE)

include("${THIRDPARTY_WEBGPU}")
cna_configure_webgpu()

get_target_property(_include WebGPU::WebGPU INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(_implib WebGPU::WebGPU IMPORTED_IMPLIB)
get_target_property(_runtime WebGPU::WebGPU IMPORTED_LOCATION)

foreach(_path IN ITEMS _include _implib _runtime)
    if(NOT EXISTS "${${_path}}")
        message(FATAL_ERROR "${_path} was not resolved from the explicit package: '${${_path}}'")
    endif()
endforeach()

if(NOT _include STREQUAL "${PACKAGE_ROOT}/include")
    message(FATAL_ERROR "Unexpected WebGPU include directory: '${_include}'")
endif()
