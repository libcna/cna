# CNA platform module (plan_platform.md PLAT-10): the CNA-owned contract that separates the
# framework from SDL3, plus the implementations of that contract.
#
# This module is the ONE place a platform library may be linked. Every other module talks to
# CNA::Platform's interfaces and never includes an SDL header -- enforced by the ratchet gate
# (cmake/PlatformRatchet.cmake, PLAT-8) and, from PLAT-121, as a hard error.
#
# src/ is flat for the contract's own types; each implementation owns a subdirectory
# (src/Sdl3/, src/Headless/, src/Terminal/) added as those phases land.
file(GLOB CNA_PLATFORM_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# Exactly one implementation is compiled, chosen by CNA_PLATFORM (cmake/PlatformSelection.cmake).
if(CNA_PLATFORM STREQUAL "SDL3")
    file(GLOB CNA_PLATFORM_IMPL_SOURCES CONFIGURE_DEPENDS "src/Sdl3/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_IMPL_SOURCES})
endif()
cna_add_module(cna_platform Platform ${CNA_PLATFORM_SOURCES})
cna_link_sharp_runtime(cna_platform PUBLIC Core.Base)

# Headers-only dependency on the core module for the CNAEXT marker macro, matching the math
# module's arrangement: no core symbol is referenced, so the link closure stays minimal.
target_link_libraries(cna_platform PUBLIC cna_core_headers)

# SDL3 is linked PRIVATE and only for the SDL3 selection: this module is the one place it may be
# linked at all, and even here it must not leak into the public include interface. The SDL-free
# probe (PLAT-27) is what proves it does not.
if(CNA_PLATFORM STREQUAL "SDL3")
    target_link_libraries(cna_platform PRIVATE SDL3::SDL3)
endif()
