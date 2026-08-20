# =====================================================================================
# CNA platform selection (plans/plan_platform.md Task PLAT-11)
#
# Chooses which CNA::Platform implementation is compiled. Exactly one is built into a
# binary; the interfaces stay virtual anyway, because the conformance suite (PLAT-116)
# needs a second implementation available in one process.
#
# Renderer and platform are SEPARATE axes. CNA_GRAPHICS_RENDERER picks how pixels are
# produced; CNA_PLATFORM picks where the window, events and input come from. Some
# combinations are invalid (a GPU renderer needs a native window handle, which the
# headless and terminal platforms do not provide), and those are rejected where the
# capability is known -- not silently tolerated until something dereferences null.
# =====================================================================================

set(CNA_PLATFORM "SDL3" CACHE STRING "Platform implementation (SDL3 | SDL2 | HEADLESS | TERMINAL)")

# Implemented today. TERMINAL is POSIX-only: it is built on termios, and there is no Windows
# console path for it (plans/plan_platform.md Phase 10). Offering it on Windows would produce a
# configure that succeeds and a build that does not.
set(_cna_platforms_available SDL3 SDL2 HEADLESS)
if(NOT WIN32)
    list(APPEND _cna_platforms_available TERMINAL)
endif()

# Reserved-but-unimplemented. These are recognised so that configuring with one fails
# LOUDLY, naming the plan, instead of silently falling back to SDL3 and producing a
# binary that is not what the user asked for. plans/plan_platform.md §12 records why each is
# out of scope; PLAT-11 is explicit that a reserved identifier must never degrade
# quietly.
set(_cna_platforms_reserved SDL12 WIN32 EMSCRIPTEN)
if(WIN32)
    list(APPEND _cna_platforms_reserved TERMINAL)
endif()

set_property(CACHE CNA_PLATFORM PROPERTY STRINGS ${_cna_platforms_available})

if(CNA_PLATFORM IN_LIST _cna_platforms_reserved)
    message(FATAL_ERROR
        "CNA: CNA_PLATFORM=${CNA_PLATFORM} is a reserved identifier that is NOT implemented.\n"
        "Only ${_cna_platforms_available} are available today.\n"
        "See plans/plan_platform.md -- SDL 1.2, native Win32 and Emscripten are recorded in "
        "\"Possible future implementations (NOT in scope)\". TERMINAL exists (Phase 10) but is "
        "POSIX-only: it is built on termios and has no Windows console path.\n"
        "This is a hard error on purpose: falling back to SDL3 would build something other "
        "than what you asked for.")
endif()

if(NOT CNA_PLATFORM IN_LIST _cna_platforms_available)
    message(FATAL_ERROR
        "CNA: CNA_PLATFORM=${CNA_PLATFORM} is not a known platform.\n"
        "Available: ${_cna_platforms_available}\n"
        "Reserved but unimplemented: ${_cna_platforms_reserved}")
endif()

message(STATUS "CNA: Using ${CNA_PLATFORM} platform implementation")

# The compile definition an implementation's own sources and the entrypoint header key
# off. Named CNA_PLATFORM_<NAME> to match the CNA_RENDERER_<NAME> convention.
add_compile_definitions(CNA_PLATFORM_${CNA_PLATFORM})
set(CNA_PLATFORM_DEFINE "CNA_PLATFORM_${CNA_PLATFORM}")
