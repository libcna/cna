# Prints the settings a toolchain file decides, one "cna-toolchain-setting: <variable>=<value>" line
# each, for cna_sdl_toolchain_settings() in cmake/SdlPrebuiltFingerprint.cmake.
#
# Run as its own `cmake -P` process, so it sees exactly what the toolchain -- and every file it
# includes -- sets, and nothing a configure adds afterwards: no language is enabled here, so
# CMake's own compiler defaults and the compilers it resolves never enter, whichever languages the
# configuring project had enabled when it asked. Values are printed byte for byte.
#
#   cmake -DCNA_SDL_TOOLCHAIN=<file> -P cmake/SdlToolchainSettings.cmake

if(NOT DEFINED CNA_SDL_TOOLCHAIN OR NOT EXISTS "${CNA_SDL_TOOLCHAIN}")
    message(FATAL_ERROR "SdlToolchainSettings.cmake: CNA_SDL_TOOLCHAIN does not name a file")
endif()

include("${CNA_SDL_TOOLCHAIN}")

foreach(_variable IN ITEMS
        CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR CMAKE_SYSTEM_VERSION CMAKE_SYSROOT
        CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_C_COMPILER_TARGET CMAKE_CXX_COMPILER_TARGET
        CMAKE_C_FLAGS_INIT CMAKE_CXX_FLAGS_INIT
        CMAKE_C_FLAGS_RELEASE_INIT CMAKE_CXX_FLAGS_RELEASE_INIT
        CMAKE_EXE_LINKER_FLAGS_INIT CMAKE_SHARED_LINKER_FLAGS_INIT CMAKE_MODULE_LINKER_FLAGS_INIT
        CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT CMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT
        CMAKE_MODULE_LINKER_FLAGS_RELEASE_INIT
        CMAKE_ANDROID_ARCH_ABI CMAKE_ANDROID_API ANDROID_ABI ANDROID_PLATFORM ANDROID_STL
        CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET)
    if(DEFINED ${_variable})
        message(NOTICE "cna-toolchain-setting: ${_variable}=${${_variable}}")
    endif()
endforeach()
