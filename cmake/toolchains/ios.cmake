# iOS toolchain for building CNA on macOS targeting iPhone/iPad (plan_apple.md, APPLE-2).
#
# Usage (from the cna repo root, on a macOS host with Xcode installed):
#
#   # Device (arm64):
#   cmake -B cmake-build-ios \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake \
#         -DCNA_GRAPHICS_RENDERER=SDL_RENDERER \
#         -DCNA_BUILD_TESTS=OFF
#   cmake --build cmake-build-ios
#
#   # Simulator (arm64 on Apple silicon, x86_64 on Intel):
#   cmake -B cmake-build-ios-sim \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake \
#         -DCNA_IOS_SIMULATOR=ON \
#         -DCNA_GRAPHICS_RENDERER=SDL_RENDERER \
#         -DCNA_BUILD_TESTS=OFF
#
# Either generator works: Ninja/Makefiles produce the .app bundles directly, and the Xcode
# generator (-G Xcode) is what you want when you need on-device signing and deployment, since
# only Xcode consumes the XCODE_ATTRIBUTE_* signing properties cmake/ApplePlatform.cmake sets.
#
# This file only settles the target platform. Deployment target, bundle metadata, signing and
# the iOS renderer allow-list live in cmake/ApplePlatform.cmake, which the top-level
# CMakeLists.txt includes for every Apple configure.

set(CMAKE_SYSTEM_NAME iOS)

option(CNA_IOS_SIMULATOR "Target the iOS simulator SDK instead of a physical device" OFF)

# Every value below is only a default: an explicitly passed -DCMAKE_OSX_SYSROOT /
# -DCMAKE_OSX_ARCHITECTURES wins. This matters for the vendored SDL sub-builds, which are
# separate cmake invocations that re-read this same toolchain file with the parent's
# sysroot/architecture handed to them on the command line (cmake/ThirdPartySDL.cmake).
if(CNA_IOS_SIMULATOR)
    if(NOT CMAKE_OSX_SYSROOT)
        set(CMAKE_OSX_SYSROOT iphonesimulator)
    endif()
    # The simulator runs native code for the host CPU: arm64 on Apple silicon, x86_64 on Intel.
    # CMAKE_HOST_SYSTEM_PROCESSOR is the host's, which is exactly the right answer here — unlike
    # the device case below, where the answer is always arm64 regardless of host.
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
else()
    if(NOT CMAKE_OSX_SYSROOT)
        set(CMAKE_OSX_SYSROOT iphoneos)
    endif()
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES arm64)
    endif()
endif()

set(CMAKE_SYSTEM_PROCESSOR "${CMAKE_OSX_ARCHITECTURES}")

# iOS has no host-executable products, so anything CMake tries to *run* during configure
# (try_run, compiler feature probes that execute) must be answered by cross-compiling rules.
set(CMAKE_CROSSCOMPILING TRUE)

# Look for headers/libraries inside the iOS SDK, never in /usr/local or /opt/homebrew — a
# Homebrew macOS dylib that leaks into an iOS link fails much later with an unreadable
# "building for iOS but attempting to link with file built for macOS" error. The repository root
# is re-rooted in as well, because CNA's own iOS-built SDL3 install (.sdl-prebuilt-iOS-*, see
# cmake/ThirdPartySDL.cmake) lives there and find_package(SDL3 CONFIG) must still reach it.
get_filename_component(_cna_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_FIND_ROOT_PATH "${_cna_repo_root}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
