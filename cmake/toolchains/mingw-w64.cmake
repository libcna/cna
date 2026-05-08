# MinGW-w64 cross-compilation toolchain for building CNA on Linux targeting Windows.
#
# Usage (from the cna repo root):
#   cmake -B build-windows \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
#         -DCNA_GRAPHICS_BACKEND=SDL_RENDERER
#   cmake --build build-windows
#
# Prerequisites:
#   sudo apt install mingw-w64   # Debian/Ubuntu
#   # SDL3 and SDL3_image/mixer pre-built for Windows must be available on CMAKE_PREFIX_PATH

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Prefer x86_64-w64-mingw32 toolchain; fall back to i686-w64-mingw32 if needed.
find_program(_MINGW_GCC NAMES x86_64-w64-mingw32-gcc)
if(_MINGW_GCC)
    set(_MINGW_TRIPLE x86_64-w64-mingw32)
else()
    set(_MINGW_TRIPLE i686-w64-mingw32)
endif()

set(CMAKE_C_COMPILER   ${_MINGW_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${_MINGW_TRIPLE}-g++)
set(CMAKE_RC_COMPILER  ${_MINGW_TRIPLE}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${_MINGW_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
