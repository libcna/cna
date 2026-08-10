# Header-only surface of the core module: the NOXNA marker (CNAHelper.hpp), the
# framework-root leaf enums (PlayerIndex) and header-only internals (PathContainment).
# Modules whose PUBLIC headers name these without using any core SYMBOL (math, storage)
# link this interface instead of the archive, keeping their accepted link closures exact.
add_library(cna_core_headers INTERFACE)
add_library(CNA::CoreHeaders ALIAS cna_core_headers)
target_include_directories(cna_core_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB CNA_CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_core Core ${CNA_CORE_SOURCES})
cna_link_sharp_runtime(cna_core PUBLIC Core.Base)
# Logger.cpp logs through SDL; the dependency is an implementation detail, not API surface.
target_link_libraries(cna_core PRIVATE SDL3::SDL3)
