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
# name before any audio call in this fresh process, then proves SDL3 reports no hardware while
# NULL succeeds without consulting it. Spawned by AudioMixerTests.cpp -- see that file and
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

# --- Task AUD-04-008: standalone mixer-destroy-with-active-voice harness ---
# A tiny standalone (non-GTest) executable that plays a SoundEffectInstance, then calls
# CNA::Internal::Audio::DestroyMixer() directly while it's still alive, then exercises every
# operation still reachable on the now-orphaned instance. Needs its own process so a genuine
# use-after-free crash (if AudioMixer.hpp's documented "no way to know about a live instance's
# lifetime" gap is real) is isolated from the shared CnaTests binary -- see the harness file's own
# top-of-file comment / tools/audio/audio_no_hardware_harness.cpp for the same "needs its own
# process" precedent.
# REMED-BUILD-005: this harness's source includes AudioMixer.hpp directly, which needs
# <SDL3/SDL.h>/<SDL3_mixer/SDL_mixer.h> -- CNA links SDL3::SDL3 PRIVATE (CnaLibrary.cmake), so that
# include path does not propagate to CNA's own consumers. Native GCC builds masked this by
# accident (this dev machine has a system-wide /usr/local/include/SDL3), but it is a hard
# `SDL3/SDL.h: No such file or directory` failure under every cross-compile toolchain (Emscripten,
# D3D9/D3D11 MinGW) that has no such fallback -- link SDL3::SDL3 explicitly instead of relying on
# transitive propagation, matching cna_devices_shutdown_ordering_harness's own pattern above.
if(CNA_BUILD_TESTS)
    add_executable(cna_audio_mixer_destroy_active_static_voice_harness
        tools/audio/mixer_destroy_active_static_voice_harness.cpp
    )
    target_link_libraries(cna_audio_mixer_destroy_active_static_voice_harness
        PRIVATE
        CNA
        SHARP_RUNTIME
        SDL3::SDL3
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
        SDL3::SDL3
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

# The cross-renderer diagnostic comparator (cna_diag_compare) is registered by its owning
# module now: modules/graphics/examples/CMakeLists.txt, next to the diagnostic scene it diffs.

# --- Task 479: CNA-side reference-value dump tool (no window/GraphicsDevice needed) -----------
# Dumps enums, state presets, PackedVector, and Viewport reference values as JSON, mirroring
# tools/fna-reference/*.cs exactly so scripts/compare-fna-reference.py can diff the two outputs.
# Not registered as a ctest: comparing against the FNA-side harness additionally requires mono/
# xbuild and a locally-built FNA.dll (tools/fna-reference/README.md), which isn't guaranteed on
# every machine that builds CNA -- this is a manually-invoked developer verification tool.
# (Source lives under tools/, so its registration lives with the other tools/ harnesses here,
# not with the module-owned examples.)
if(CNA_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_reference_dump tools/cna-reference/CnaReferenceDump.cpp)
    target_link_libraries(cna_reference_dump PRIVATE CNA)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_reference_dump PRIVATE SDL3::SDL3main)
    endif()
endif()

# --- Task VERIFY-003/DEV-API-002: strict XNA API surface compile check ---
# Compiles tools/devices/StrictXnaApiSurfaceCheck.cpp with CNA_STRICT_XNA_API
# defined (turns the CNAEXT macro into [[deprecated]], see CNAHelper.hpp) and
# -Werror=deprecated-declarations, so this target fails to build if that file
# ever calls a CNAEXT-tagged Microsoft::Devices/Sensors member. A standalone
# executable, not a gtest binary — same tools/ precedent as
# cna_net_two_process_harness above, for the same reason (its own main()).
if(CNA_BUILD_TESTS AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
    add_executable(cna_strict_xna_api_check
        tools/devices/StrictXnaApiSurfaceCheck.cpp
    )
    target_link_libraries(cna_strict_xna_api_check PRIVATE CNA)
    target_compile_definitions(cna_strict_xna_api_check PRIVATE CNA_STRICT_XNA_API)
    target_compile_options(cna_strict_xna_api_check PRIVATE -Werror=deprecated-declarations)
    if(MINGW)
        # REMED-BUILD-013: without this, the .exe links fine but fails to *run* under Wine --
        # "libgcc_s_seh-1.dll/libstdc++-6.dll not found" (status c0000135) -- matching the same
        # MinGW-runtime treatment every cna_directx9_test()/cna_directx11_test() executable already gets.
        target_link_options(cna_strict_xna_api_check PRIVATE -static-libgcc -static-libstdc++)
        target_link_options(cna_strict_xna_api_check PRIVATE -Wl,--allow-multiple-definition)
        cna_copy_mingw_runtime(cna_strict_xna_api_check)
        cna_copy_sdl_runtime(cna_strict_xna_api_check)
    endif()

    # REMED-BUILD-013: this target builds fine under the D3D9/D3D11/D3D12 MinGW cross-compile (the
    # CMAKE_CXX_COMPILER_ID guard above already admits it), but its own add_test() below is a plain
    # Windows PE executable command with no Wine wrapper -- unlike CnaTests (cmake/UnitTests.cmake),
    # cna_strict_xna_api_check never gets a CROSSCOMPILING_EMULATOR target property, so ctest tries
    # to exec the .exe natively on the Linux host ("unable to find an interpreter"). Never creates a
    # D3D9/D3D11/D3D12 device (pure link-time API-surface check, no window/GPU), so it needs the same
    # *_SKIP_*_GATE=1 each renderer's own device-free CnaTests invocation already uses.
    if(CMAKE_CROSSCOMPILING)
        if(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX9")
            set_target_properties(cna_strict_xna_api_check PROPERTIES CROSSCOMPILING_EMULATOR
                "env;CNA_D3D9_SKIP_DXVK_GATE=1;${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk9.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
            set_target_properties(cna_strict_xna_api_check PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_D3D11_SKIP_DXVK_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12")
            set_target_properties(cna_strict_xna_api_check PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_D3D12_SKIP_VKD3D_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-vkd3d.sh")
        endif()
    endif()
    add_test(NAME StrictXnaApiSurfaceCheck_Compile_Run COMMAND cna_strict_xna_api_check)

    # --- Task TEST2-010: negative counterpart -- a deliberately leaked CNAEXT call must fail ---
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
    # itself exits non-zero (i.e. only if the deliberate CNAEXT call above genuinely fails to
    # compile, exactly as it must). If a future CNAEXT/CNA_STRICT_XNA_API regression ever let
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

# --- SKIA-159: standalone Ganesh/OpenGL artifact probe ---
# Throwaway (per this project's build-hygiene convention) proof that the separately pinned
# Ganesh/OpenGL Skia artifact (docs/skia-ganesh-artifact.md) actually links and constructs a real
# GrDirectContext via GrDirectContexts::MakeGL() over a real SDL GL context. Only built when
# -DCNA_SKIA_GANESH_BUILD_DIR is explicitly set; the ordinary CNA_GRAPHICS_RENDERER=SKIA (raster)
# build never sets it, so this target does not exist there and the validated raster artifact/build
# graph is unaffected. Not wired into CTest: SKIA-160/161 own construction-time mode selection and
# the real renderer integration; this is strictly a below-the-API artifact-correctness probe,
# matching this renderer's established "prove it below the API first" sequencing.
if(CNA_SKIA_GANESH_BUILD_DIR)
    include(cmake/ThirdPartySkiaGanesh.cmake)
    cna_configure_skia_ganesh()

    add_executable(cna_skia_ganesh_artifact_probe
        tools/skia/skia_ganesh_artifact_probe.cpp
    )
    # SKIA-161: matches cmake/Tests/SkiaTests.cmake's cna_skia_test() macro's own exception --
    # the pinned no-RTTI Skia archives cannot provide the typeinfo required by `vptr`, so linking
    # this target under UBSan fails with "undefined reference to typeinfo for GrDirectContext"
    # without it. Found by an actual ASan+UBSan build of cmake-build-skia-ganesh-asan, not
    # reasoned in advance; every other Skia-linked executable already had this exception because
    # it goes through cna_skia_test(), which this Harnesses.cmake-registered target does not.
    if(CNA_SANITIZE MATCHES "(^|,)undefined(,|$)"
       AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(cna_skia_ganesh_artifact_probe PRIVATE -fno-sanitize=vptr)
    endif()
    target_link_libraries(cna_skia_ganesh_artifact_probe
        PRIVATE
        CNA::SkiaGanesh
        SDL3::SDL3
    )
endif()

# --- plan_fx.md FX-051: compiled Effect Framework fuzz harness ---
# One entry point covering construction, reflection, clone, technique/pass selection, apply and
# disposal of an untrusted compiled effect binary. Built by default in its standalone replay
# shape, which is how a committed corpus is exercised and how a campaign's crashing input is
# reproduced; CNA_FX_FUZZER_ENTRY_POINT=ON drops main() so clang's libFuzzer (or AFL++ in its
# libFuzzer compatibility mode) can own the loop instead. Not registered as a ctest test: it
# needs a real graphics device and a corpus path, and the deterministic corpus that does run on
# every build lives in the FNA3D compiled-effect suite.
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT ANDROID)
    option(CNA_FX_FUZZER_ENTRY_POINT
           "Build the compiled-effect fuzz harness for libFuzzer/AFL++ instead of standalone replay"
           OFF)
    add_executable(cna_compiled_effect_fuzzer tools/graphics/compiled_effect_fuzzer.cpp)
    target_link_libraries(cna_compiled_effect_fuzzer PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
    if(CNA_FX_FUZZER_ENTRY_POINT)
        target_compile_definitions(cna_compiled_effect_fuzzer PRIVATE CNA_FX_FUZZER_ENTRY_POINT)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_link_options(cna_compiled_effect_fuzzer PRIVATE -fsanitize=fuzzer)
        else()
            # AFL++'s afl-clang-lto/afl-gcc-fast supply their own driver, so a missing libFuzzer
            # is only fatal when nothing else provides main().
            message(WARNING
                "CNA: CNA_FX_FUZZER_ENTRY_POINT=ON without clang -- the fuzz driver must be "
                "supplied by the toolchain (for example AFL++) or the link will fail.")
        endif()
    endif()
endif()

# --- plan_fx.md FX-053: compiled Effect Framework performance baseline ---
# Measures construction, clone, dirty upload, clean apply and draw so the immutable-artifact-cache
# question is decided on numbers. Manually invoked and never registered with ctest: wall-clock
# timings on a shared machine are a baseline to compare, not a pass/fail signal.
if(CNA_BUILD_TESTS AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_compiled_effect_benchmark tools/graphics/compiled_effect_benchmark.cpp)
    target_link_libraries(cna_compiled_effect_benchmark PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)
endif()

# plan_fx.md FX-061/FX-062/FX-063/FX-065 existence gate: proves the pinned MojoShader parses a
# compiled Effect Framework binary while linking only MojoShader -- no FNA3D and no CNA. Every
# backend planned after FNA3D depends on that being true, and it was not obvious: MojoShader is
# FNA3D's submodule, its include root is absent from FNA3D's install surface, and its header hides
# every Effect Framework struct unless the right switches are defined.
#
# Built only where cna_mojoshader exists, which today means a configuration that fetched FNA3D.
# Separating the target is the first half of decoupling those two; the fetch is the second and is
# not needed until a non-FNA3D backend is actually implemented.
if(CNA_BUILD_TESTS AND TARGET cna_mojoshader AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_mojoshader_effect_probe tools/graphics/mojoshader_effect_probe.cpp)
    target_link_libraries(cna_mojoshader_effect_probe PRIVATE cna_mojoshader SDL3::SDL3)
    add_test(NAME cna_mojoshader_effect_probe
             COMMAND cna_mojoshader_effect_probe
                     "${CMAKE_CURRENT_SOURCE_DIR}/modules/renderers/fna3d/effects/BasicEffect.fxb"
                     "${CMAKE_CURRENT_SOURCE_DIR}/modules/renderers/fna3d/effects/CnaConformanceEffect.fxb")
endif()

# plan_fx.md FX-061 existence gate: proves the pinned MojoShader's SDL_GPU adapter binds a
# committed effect's shader pairs against a device this machine can create, linking only MojoShader
# and SDL3. CNA's SDL_GPU renderer already builds pipelines from SPIR-V and MojoShader has both a
# SPIR-V profile and an SDL_GPU adapter, so the pairing looks obvious on paper -- what the probe
# settles is whether it links real effect shaders and how much uniform plumbing the adapter owns,
# which is what sizes the task.
#
# Not registered with ctest: it needs a working GPU device, which a headless CI runner may not
# have, and a missing device is not a CNA regression.
if(CNA_BUILD_TESTS AND TARGET cna_mojoshader AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_mojoshader_sdlgpu_probe tools/graphics/mojoshader_sdlgpu_probe.cpp)
    target_link_libraries(cna_mojoshader_sdlgpu_probe PRIVATE cna_mojoshader SDL3::SDL3)
endif()

# plan_fx.md FX-062 existence gate: proves the pinned MojoShader's OpenGL adapter (mojoshader_
# opengl.c) links and renders a committed effect's shader pair against a real GLES3 context this
# machine can create, linking only MojoShader and SDL3 -- no CNA, no EasyGL. EasyGL is the shared
# implementation behind OPENGLES2/OPENGLES3/OPENGL33/OPENGL4/WEBGL1/WEBGL2, and its own stock
# shaders are authored once in GLSL ES 3.00, but that string-rewriting pipeline is irrelevant to
# MojoShader-compiled shaders: MojoShader emits already-correct-dialect GLSL for whichever profile
# its own MOJOSHADER_glCreateContext is asked for, entirely in parallel to EasyGL's own shaders.
#
# Not registered with ctest: it needs a working GL-capable display, which a headless CI runner may
# not have, and a missing display is not a CNA regression. Does not require CNA_EASYGL_COMPILED_
# EFFECTS or even the EasyGL renderer to be selected -- only cna_mojoshader (any renderer that
# enables its own compiled-effects option publishes that target) and SDL3's GL context support.
if(CNA_BUILD_TESTS AND TARGET cna_mojoshader AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_mojoshader_gl_probe tools/graphics/mojoshader_gl_probe.cpp)
    target_link_libraries(cna_mojoshader_gl_probe PRIVATE cna_mojoshader SDL3::SDL3)
endif()

# --- plan_platform.md PLAT-131: terminal restoration harness ---
# A tiny standalone (non-GTest) executable that takes the terminal over with a TerminalSession and
# then dies in a chosen way: normally, by SIGINT/SIGTERM/SIGHUP, by abort(), or by letting an
# exception escape main. Four of those five destroy the process, so none can be asserted inside
# the shared CnaTests binary -- the assertion would die with it. TerminalRestorationTests.cpp
# spawns this under a pseudo-terminal it owns and checks the terminal came back afterwards. Same
# "needs its own process" precedent as cna_net_two_process_harness and
# cna_devices_shutdown_ordering_harness above.
#
# POSIX-only, for the same reason TerminalSession itself is: it is built on termios.
if(CNA_BUILD_TESTS AND NOT WIN32)
    add_executable(cna_platform_terminal_restoration_harness
        tools/platform/terminal_restoration_harness.cpp
    )
    # Links the platform module rather than all of CNA: TerminalSession has no dependency beyond
    # libc and the platform module's own exception type, and keeping the harness's link closure
    # small keeps a failure in it attributable to what it is testing.
    target_link_libraries(cna_platform_terminal_restoration_harness
        PRIVATE
        cna_platform
    )
endif()

# --- plan_platform.md PLAT-136: terminal resize harness ---
# TerminalPlatform installs its SIGWINCH watcher only when attached to a terminal and reads the new
# size from its own stdout -- neither of which holds inside CnaTests, where CI redirects output. So
# the resize assertions run here, in a process whose standard descriptors really are a
# pseudo-terminal, spawned by TerminalResizeTests.cpp. Without it the two tests that matter most in
# that file would skip in CI, and a test that always skips proves nothing.
if(CNA_BUILD_TESTS AND NOT WIN32)
    add_executable(cna_platform_terminal_resize_harness
        tools/platform/terminal_resize_harness.cpp
    )
    target_link_libraries(cna_platform_terminal_resize_harness
        PRIVATE
        cna_platform
    )
endif()
