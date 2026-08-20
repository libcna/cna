# Header-only surface of the core module: the CNAEXT marker (CNAHelper.hpp), the
# framework-root leaf enums (PlayerIndex) and header-only internals (PathContainment).
# Modules whose PUBLIC headers name these without using any core SYMBOL (math, storage)
# link this interface instead of the archive, keeping their accepted link closures exact.
add_library(cna_core_headers INTERFACE)
add_library(CNA::CoreHeaders ALIAS cna_core_headers)
target_include_directories(cna_core_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The generated release identity (CNA/Version.hpp, cmake/Version.cmake) is part of core's
# public header surface, so it rides the same include roots as the checked-in headers -- a
# consumer includes "CNA/Version.hpp" without knowing which of the two roots resolves it.
target_include_directories(cna_core_headers INTERFACE ${CNA_GENERATED_INCLUDE_DIR})

file(GLOB CNA_CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_core Core ${CNA_CORE_SOURCES})
cna_link_sharp_runtime(cna_core PUBLIC Core.Base)
target_include_directories(cna_core PUBLIC ${CNA_GENERATED_INCLUDE_DIR})

# GraphicsRendererSelectionEmscripten.cpp (RTR-P10-23) reads Module.cnaPreferredRenderer through
# emscripten::val, whose implementation lives in libembind. Emscripten does not link that library
# implicitly, so without this every Emscripten executable fails at wasm-ld with undefined _emval_*
# symbols. The dependency belongs to this archive, so it travels with it rather than being repeated
# in each consumer's link line.
if(EMSCRIPTEN)
    target_link_options(cna_core INTERFACE "-lembind")
endif()
# No SDL: plan_platform.md PLAT-53 moved Logger onto its own sink and PLAT-54 moved
# Entrypoint.hpp into the platform module, so nothing under modules/core includes an SDL header
# any more. This is the first module to come off SDL entirely.
