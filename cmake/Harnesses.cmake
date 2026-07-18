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

# --- Task SDLCORE-011: standalone SDL_Quit()-ordering harness ---
# A tiny standalone (non-GTest) executable that touches
# VibrateController::getDefaultProperty()'s function-local static singleton, then calls the real
# SDL_Quit() before main() returns (which is when that singleton's destructor actually runs, as
# part of process-exit static teardown) -- reproducing the exact ordering hazard
# Detail::DevicesShutdownCoordinator exists to close. The shared CnaTests binary cannot exercise
# this: calling the real SDL_Quit() there would tear down SDL process-wide for every other test
# sharing that binary. See tools/devices/shutdown_ordering_harness.cpp's own top-of-file comment
# for the "--skip-shutdown-call" flag used to confirm under ASan that this reproduces a genuine
# heap-use-after-free when the fix's guard is bypassed.
if(CNA_BUILD_TESTS)
    add_executable(cna_devices_shutdown_ordering_harness
        tools/devices/shutdown_ordering_harness.cpp
    )
    target_link_libraries(cna_devices_shutdown_ordering_harness
        PRIVATE
        CNA
        SHARP_RUNTIME
        SDL3::SDL3
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

    # --- Task TEST2-010: negative counterpart -- a deliberately leaked NOXNA call must fail ---
    # EXCLUDE_FROM_ALL: this target's whole point is to fail to compile, so it must never be
    # part of the normal `cmake --build .`/`--target all` (or CnaTests' own dependency graph) --
    # only the ctest below ever builds it, on purpose, expecting that build to fail.
    add_executable(cna_strict_xna_api_leak_check EXCLUDE_FROM_ALL
        tools/devices/StrictXnaApiSurfaceLeakCheck.cpp
    )
    target_link_libraries(cna_strict_xna_api_leak_check PRIVATE CNA)
    target_compile_definitions(cna_strict_xna_api_leak_check PRIVATE CNA_STRICT_XNA_API)
    target_compile_options(cna_strict_xna_api_leak_check PRIVATE -Werror=deprecated-declarations)

    # Invokes the actual build of the EXCLUDE_FROM_ALL target above as this test's own command
    # -- WILL_FAIL TRUE means ctest reports this test as PASSING only if that build command
    # itself exits non-zero (i.e. only if the deliberate NOXNA call above genuinely fails to
    # compile, exactly as it must). If a future NOXNA/CNA_STRICT_XNA_API regression ever let
    # this target build successfully, this test would flip to FAILING, catching it.
    add_test(NAME StrictXnaApiSurfaceLeakCheck_MustFailToCompile
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target cna_strict_xna_api_leak_check --config $<CONFIG>
    )
    set_tests_properties(StrictXnaApiSurfaceLeakCheck_MustFailToCompile PROPERTIES WILL_FAIL TRUE)
endif()

# --- Task PERF2-001: Devices microbenchmark suite ---
# A tiny standalone (non-GTest) executable emitting JSON-Lines latency-percentile results for
# this task's own named categories. Not registered as a ctest (its output is meant to be
# captured/compared, not pass/failed on its own exit code) -- run manually or via
# tools/devices/compare_devices_microbenchmark.py, see that file and
# devices_microbenchmark.cpp's own top-of-file comment.
if(CNA_BUILD_TESTS)
    add_executable(cna_devices_microbenchmark
        tools/devices/devices_microbenchmark.cpp
    )
    target_link_libraries(cna_devices_microbenchmark
        PRIVATE
        CNA
        SHARP_RUNTIME
    )
endif()
