# Header-only surface of the core module: the CNAEXT marker (CNAHelper.hpp), the
# framework-root leaf enums (PlayerIndex) and header-only internals (PathContainment).
# Modules whose PUBLIC headers name these without using any core SYMBOL (math, storage)
# link this interface instead of the archive, keeping their accepted link closures exact.
add_library(cna_core_headers INTERFACE)
add_library(CNA::CoreHeaders ALIAS cna_core_headers)
target_include_directories(cna_core_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB CNA_CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_core Core ${CNA_CORE_SOURCES})
cna_link_sharp_runtime(cna_core PUBLIC Core.Base)
# Logger.cpp uses SDL internally, but SDL is also part of the public compile surface:
# CNA/Entrypoint.hpp includes SDL_main.h on SDL/mobile targets. Consumers linking CNA::Core or the
# CNA umbrella must therefore inherit SDL's include directory without adding a hidden extra
# dependency themselves.
target_link_libraries(cna_core PUBLIC SDL3::SDL3)

# Some SDL builds package the platform-owned main() implementation as a separate target. Publish
# it with the header that renames the game's main() so a downstream iOS/Android executable cannot
# compile successfully and then fail its final link on a missing SDL entry point.
if(TARGET SDL3::SDL3main)
    target_link_libraries(cna_core PUBLIC SDL3::SDL3main)
endif()
