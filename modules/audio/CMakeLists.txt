# FrameworkDispatcher.cpp lives in this module's src/Xna/: its implementation state is the
# audio stream list, and audio/media/runtime all call INTO it (ownership follows dependency
# direction; assigning it to runtime would create a runtime<->audio link cycle for nothing).
file(GLOB_RECURSE CNA_AUDIO_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
# The independent audio selection owns its native edge. The portable NullAudioDevice is always
# compiled so the default build can run cross-implementation conformance tests.  Native sources
# and libraries must follow the selected backend; otherwise a nominal SDL2-only binary would
# silently retain an SDL3/SDL3_mixer link through cna_audio.
if(NOT CNA_AUDIO_PLATFORM STREQUAL "SDL3")
    list(FILTER CNA_AUDIO_SOURCES EXCLUDE REGEX ".*/src/Platform/Sdl3/.*\\.cpp$")
    list(FILTER CNA_AUDIO_SOURCES EXCLUDE REGEX ".*/src/Backend/Sdl3Mixer/.*\\.cpp$")
endif()
if(NOT CNA_AUDIO_PLATFORM STREQUAL "SDL2")
    list(FILTER CNA_AUDIO_SOURCES EXCLUDE REGEX ".*/src/Platform/Sdl2/.*\\.cpp$")
endif()
# plans/plan_x11.md X11-0151: ALSA playback and CNA's own mixer, the SDL-free pair. The mixer is
# the other implementation of MixerEngine.hpp, so it must never be compiled beside Sdl3Mixer.
if(NOT CNA_AUDIO_PLATFORM STREQUAL "ALSA")
    list(FILTER CNA_AUDIO_SOURCES EXCLUDE REGEX ".*/src/Platform/Alsa/.*\\.cpp$")
    list(FILTER CNA_AUDIO_SOURCES EXCLUDE REGEX ".*/src/Backend/CnaMixer/.*\\.cpp$")
endif()
if(CNA_AUDIO_PLATFORM STREQUAL "ALSA")
    # Headers only: libasound is loaded at run time (Platform/Alsa/AlsaAudioDevice.cpp), so a
    # machine without it still runs the program and reports no audio hardware instead.
    find_package(ALSA QUIET)
    if(NOT ALSA_INCLUDE_DIR OR NOT EXISTS "${ALSA_INCLUDE_DIR}/alsa/asoundlib.h")
        message(FATAL_ERROR
            "CNA: CNA_AUDIO_PLATFORM=ALSA needs the ALSA development headers "
            "(Debian/Ubuntu: libasound2-dev; Fedora: alsa-lib-devel).")
    endif()
endif()
cna_add_module(cna_audio Audio ${CNA_AUDIO_SOURCES})
target_include_directories(cna_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cna_audio PUBLIC cna_core cna_diagnostics cna_math)
# XACT title-content bytes are read through the selected platform filesystem so packaged Android
# assets and ordinary desktop paths share one loader without introducing a runtime -> audio cycle.
target_link_libraries(cna_audio PRIVATE cna_platform)
cna_link_sharp_runtime(cna_audio PUBLIC Core.Base IO Runtime)
if(CNA_AUDIO_PLATFORM STREQUAL "SDL3")
    target_link_libraries(cna_audio PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)
elseif(CNA_AUDIO_PLATFORM STREQUAL "SDL2")
    target_link_libraries(cna_audio PRIVATE SDL2::SDL2)
elseif(CNA_AUDIO_PLATFORM STREQUAL "ALSA")
    target_include_directories(cna_audio PRIVATE ${ALSA_INCLUDE_DIR} ${CNA_SOURCE_DIR}/third_party/stb
        ${CNA_SOURCE_DIR}/third_party/dr_libs)
    target_link_libraries(cna_audio PRIVATE ${CMAKE_DL_LIBS})
    # stb_vorbis and dr_libs (plans/plan_x11.md X11-0161) are third-party code kept identical to
    # upstream; their warnings are not ours to fix.
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/Backend/CnaMixer/StbVorbis.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Backend/CnaMixer/DrLibs.cpp"
        PROPERTIES COMPILE_OPTIONS "-w")
elseif(NOT CNA_AUDIO_PLATFORM STREQUAL "NULL")
    message(FATAL_ERROR "CNA: cna_audio has no link rule for CNA_AUDIO_PLATFORM=${CNA_AUDIO_PLATFORM}")
endif()
# Genuine XNA-semantic static-archive cycle (kept, not redesigned): FrameworkDispatcher.Update()
# pumps both the audio streams and MediaPlayer (MediaPlayer::Update/OnActiveSongChanged/
# OnMediaStateChanged), while MediaPlayer itself plays through the audio mixer. Declared on both
# targets (see the media module) so CMake repeats the archives.
target_link_libraries(cna_audio PRIVATE cna_media)
# FrameworkDispatcher::Update() also pumps TouchPanel (TouchDeviceExists check + gesture
# update), exactly as FNA's FrameworkDispatcher.Update() does. This audio -> input symbol
# edge previously resolved only through the transitive link closure (media -> graphics-core
# -> $<LINK_ONLY:cna_input>); declare it directly so the build graph states the real
# dependency instead of relying on another module's private edge. Plain DAG edge, no cycle:
# input has no path back to audio.
target_link_libraries(cna_audio PRIVATE cna_input)

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
