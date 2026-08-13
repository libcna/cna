# =====================================================================================
# CNA audio-platform selection (plan_platform.md Task PLAT-93)
#
# Independent from CNA_PLATFORM: a headless/terminal application may still use SDL3 audio,
# while a graphical SDL3 application may deliberately select deterministic NULL audio.
# Implementations consume CNA_AUDIO_PLATFORM_<NAME>; later Phase 6 tasks own their sources.
# =====================================================================================

# `cmake -P` does not inherit the project's cmake_minimum_required() policy set. Keep the same
# IN_LIST semantics in the lightweight selection tests and in a normal project include.
cmake_policy(SET CMP0057 NEW)

set(CNA_AUDIO_PLATFORM "SDL3" CACHE STRING "Audio platform implementation (SDL3 | NULL)")

set(_cna_audio_platforms_available SDL3 NULL)

# Recognized future directions, deliberately not aliases for SDL3. A misspelled or premature
# selection must fail at configure time instead of producing a binary with an unintended backend.
set(_cna_audio_platforms_reserved OPENAL WASAPI ALSA)

set_property(CACHE CNA_AUDIO_PLATFORM PROPERTY STRINGS ${_cna_audio_platforms_available})

if(CNA_AUDIO_PLATFORM IN_LIST _cna_audio_platforms_reserved)
    message(FATAL_ERROR
        "CNA: CNA_AUDIO_PLATFORM=${CNA_AUDIO_PLATFORM} is a reserved identifier that is NOT implemented.\n"
        "Only ${_cna_audio_platforms_available} are available today.\n"
        "See plan_platform.md Phase 6 and section 12 -- OpenAL, WASAPI and ALSA are future "
        "audio implementations, not fallbacks.\n"
        "This is a hard error on purpose: falling back to SDL3 would build something other "
        "than what you asked for.")
endif()

if(NOT CNA_AUDIO_PLATFORM IN_LIST _cna_audio_platforms_available)
    message(FATAL_ERROR
        "CNA: CNA_AUDIO_PLATFORM=${CNA_AUDIO_PLATFORM} is not a known audio platform.\n"
        "Available: ${_cna_audio_platforms_available}\n"
        "Reserved but unimplemented: ${_cna_audio_platforms_reserved}")
endif()

message(STATUS "CNA: Using ${CNA_AUDIO_PLATFORM} audio platform implementation")

# `cmake -P` is used by the selection contract tests below. Directory compile commands are not
# scriptable, so the tests exercise all validation above and the real configure adds the define.
if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_compile_definitions(CNA_AUDIO_PLATFORM_${CNA_AUDIO_PLATFORM})
endif()
set(CNA_AUDIO_PLATFORM_DEFINE "CNA_AUDIO_PLATFORM_${CNA_AUDIO_PLATFORM}")
