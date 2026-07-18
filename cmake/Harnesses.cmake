# --- Task 6.1: real two-process ENet loopback test harness ---
# A tiny standalone (non-GTest) executable playing "host" or "client", spawned twice as
# independent OS processes by tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp. See NEXT.md /
# the approved Phase 6 plan for why this exists separately from every Phase 5 single-process test.
# Uses POSIX-only <sys/resource.h>/posix_spawn -- excluded on the same platforms its own consumer
# test (TwoProcessLoopbackTest.cpp) already is, above, rather than only excluding the test source
# and leaving this executable target to fail the build on its own.
if(CNA_ENABLE_NET AND CNA_BUILD_TESTS AND NOT (WIN32 OR EMSCRIPTEN OR ANDROID))
    add_executable(cna_net_two_process_harness
        tools/net/net_two_process_harness.cpp
    )
    target_link_libraries(cna_net_two_process_harness
        PRIVATE
        CNA_GamerServices
        CNA_Net
        SHARP_RUNTIME
    )
endif()

# --- Task 12.1: GamerServicesDispatcher hang regression harness ---
# A tiny standalone (non-GTest) executable that calls GamerServicesDispatcher::Initialize() then
# NetworkSession::Create(), reproducing DEFERRED.md item #19 (../cna-samples) if it regresses.
# Must be a separate process, same reasoning as cna_net_two_process_harness above but for a
# different process-global-state hazard — see the harness file's own top-of-file comment.
if(CNA_ENABLE_NET AND CNA_BUILD_TESTS)
    add_executable(cna_net_gamerservices_dispatcher_harness
        tools/net/gamerservices_dispatcher_harness.cpp
    )
    target_link_libraries(cna_net_gamerservices_dispatcher_harness
        PRIVATE
        CNA_GamerServices
        CNA_Net
        SHARP_RUNTIME
    )
endif()

# --- Task P9-HARDWARE-005: standalone no-audio-hardware harness ---
# A tiny standalone (non-GTest) executable that forces SDL_AUDIODRIVER to a nonexistent driver
# name before any SDL audio call in this fresh process, then proves NoAudioHardwareException is
# genuinely thrown. Spawned by tests/CNA/Internal/Audio/AudioMixerTests.cpp -- see that file /
# tools/audio/audio_no_hardware_harness.cpp for why this needs its own process (AudioMixer.cpp's
# g_mixer is a process-wide, once-ever-initialized cache).
if(CNA_BUILD_TESTS)
    add_executable(cna_audio_no_hardware_harness
        tools/audio/audio_no_hardware_harness.cpp
    )
    target_link_libraries(cna_audio_no_hardware_harness
        PRIVATE
        CNA
        SHARP_RUNTIME
    )
endif()

# --- Task AUD-04-008: standalone mixer-destroy-with-active-voice harness ---
# A tiny standalone (non-GTest) executable that plays a SoundEffectInstance, then calls
# CNA::Internal::Audio::DestroyMixer() directly while it's still alive, then exercises every
# operation still reachable on the now-orphaned instance. Needs its own process so a genuine
# use-after-free crash (if AudioMixer.hpp's documented "no way to know about a live instance's
# lifetime" gap is real) is isolated from the shared CnaTests binary -- see the harness file's own
# top-of-file comment / tools/audio/audio_no_hardware_harness.cpp for the same "needs its own
# process" precedent.
if(CNA_BUILD_TESTS)
    add_executable(cna_audio_mixer_destroy_active_static_voice_harness
        tools/audio/mixer_destroy_active_static_voice_harness.cpp
    )
    target_link_libraries(cna_audio_mixer_destroy_active_static_voice_harness
        PRIVATE
        CNA
        SHARP_RUNTIME
    )
endif()

# --- Task AUD-04-009: standalone mixer-destroy-with-active-dynamic-voice harness ---
# The DynamicSoundEffectInstance counterpart to cna_audio_mixer_destroy_active_static_voice_harness
# just above -- same hazard/fix, but exercised through DynamicSoundEffectInstance's own independent
# track_ access sites. See the harness file's own top-of-file comment for why this needs a separate
# harness from the static-voice one, not just a shared one.
if(CNA_BUILD_TESTS)
    add_executable(cna_audio_mixer_destroy_active_dynamic_voice_harness
        tools/audio/mixer_destroy_active_dynamic_voice_harness.cpp
    )
    target_link_libraries(cna_audio_mixer_destroy_active_dynamic_voice_harness
        PRIVATE
        CNA
        SHARP_RUNTIME
    )
endif()

# --- plan_audio.md AUD-06-025: standalone XNB SoundEffect metadata inspection tool ---
# A small standalone (non-GTest) executable that loads a .xnb SoundEffect asset through the real
# production ContentManager::Load<SoundEffect>() path and emits its metadata (name, decoded
# duration) as stable single-line JSON, without ever calling Play()/CreateInstance() -- for
# debugging and CI manifests. See the tool's own top-of-file comment for its exact JSON shape.
if(CNA_BUILD_TESTS)
    add_executable(cna_xnb_audio_metadata_dump
        tools/audio/xnb_audio_metadata_dump.cpp
    )
    target_link_libraries(cna_xnb_audio_metadata_dump
        PRIVATE
        CNA
        SHARP_RUNTIME
    )
endif()

# --- plan_audio.md AUD-11-028: standalone XWB wave-bank inspection/extraction tool ---
# A small standalone (non-GTest) executable that parses a .xwb wave bank via the real
# CNA::Internal::Audio::ParseXwb() and emits every entry's metadata as stable JSON, optionally
# also exporting each entry's real audio payload as a playable .wav file (via the same shared
# WavWrapper technique WaveBank.cpp itself uses) for offline diagnosis. Never plays anything.
if(CNA_BUILD_TESTS)
    add_executable(cna_xwb_inspect
        tools/audio/xwb_inspect.cpp
    )
    target_link_libraries(cna_xwb_inspect
        PRIVATE
        CNA
        SHARP_RUNTIME
    )
endif()

# --- plan_software.md SOFTWARE-61/84: cross-backend diagnostic comparator ---
# Standalone, backend-independent (no CNA/SHARP_RUNTIME dependency) tool that diffs two raw
# 64x64 RGBA8 dumps produced by cross_backend_diagnostic_scene.cpp (built once per backend, see
# the SOFTWARE and EASYGL sections below) within a tolerance. Not wired into a single ctest run --
# CNA_GRAPHICS_BACKEND is a compile-time choice, so comparing two backends' output needs two
# separate builds' dumps; see docs/software-backend.md's "Cross-backend diagnostic" section for
# the exact manual invocation.
if(CNA_BUILD_TESTS)
    add_executable(cna_diag_compare examples/cross_backend_diagnostic_compare.cpp)
endif()

# --- Task VERIFY-003/DEV-API-002: strict XNA API surface compile check ---
# Compiles tools/devices/StrictXnaApiSurfaceCheck.cpp with CNA_STRICT_XNA_API
# defined (turns the NOXNA macro into [[deprecated]], see CNAHelper.hpp) and
# -Werror=deprecated-declarations, so this target fails to build if that file
# ever calls a NOXNA-tagged Microsoft::Devices/Sensors member. A standalone
# executable, not a gtest binary — same tools/ precedent as
# cna_net_two_process_harness above, for the same reason (its own main()).
if(CNA_BUILD_TESTS AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    add_executable(cna_strict_xna_api_check
        tools/devices/StrictXnaApiSurfaceCheck.cpp
    )
    target_link_libraries(cna_strict_xna_api_check PRIVATE CNA)
    target_compile_definitions(cna_strict_xna_api_check PRIVATE CNA_STRICT_XNA_API)
    target_compile_options(cna_strict_xna_api_check PRIVATE -Werror=deprecated-declarations)
    add_test(NAME StrictXnaApiSurfaceCheck_Compile_Run COMMAND cna_strict_xna_api_check)
endif()
