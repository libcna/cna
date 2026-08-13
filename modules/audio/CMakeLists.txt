# FrameworkDispatcher.cpp lives in this module's src/Xna/: its implementation state is the
# audio stream list, and audio/media/runtime all call INTO it (ownership follows dependency
# direction; assigning it to runtime would create a runtime<->audio link cycle for nothing).
file(GLOB_RECURSE CNA_AUDIO_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
# The independent audio selection owns its native edge. The portable NullAudioDevice is always
# compiled so the default build can run cross-implementation conformance tests; a non-SDL3
# selection must not accidentally compile or construct the native SDL3 implementation.
if(NOT CNA_AUDIO_PLATFORM STREQUAL "SDL3")
    list(FILTER CNA_AUDIO_SOURCES EXCLUDE REGEX ".*/src/Platform/Sdl3/.*\\.cpp$")
endif()
cna_add_module(cna_audio Audio ${CNA_AUDIO_SOURCES})
target_include_directories(cna_audio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cna_audio PUBLIC cna_core cna_math)
cna_link_sharp_runtime(cna_audio PUBLIC Core.Base IO Runtime)
target_link_libraries(cna_audio PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)
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
