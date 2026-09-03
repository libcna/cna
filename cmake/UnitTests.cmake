# plans/plan_runtimerenderer.md RTR-P9-19: renderer gates that decide whether a renderer's OWN tests are
# compiled, or its libraries exposed to the test executable, test LIST MEMBERSHIP
# (CNA_RENDERER_IDENTITIES) rather than equality with the build default. In single-renderer mode the
# list holds one entry and the two are identical; in a multi-renderer build a renderer's tests must
# be kept whenever that renderer is compiled in, not only when it happens to be the default --
# otherwise a multi build silently drops the suites of every non-default renderer it contains.
#
# The CROSSCOMPILING_EMULATOR chain further down is deliberately NOT converted: a target carries one
# emulator property, so it can only ever describe one renderer, and the default is the honest choice.

option(CNA_ENABLE_PCH
    "Enable the measured precompiled-header pilot for the content unit-test object target" OFF)

if(CNA_BUILD_TESTS)
    # Task DEVPERF-001: fail fast with an actionable message rather than
    # CMake's own generic "add_subdirectory given source ... which is not an
    # existing directory" if this submodule was never initialized (e.g. a
    # plain ZIP/tarball export of this source tree, which cannot contain
    # submodule content at all, or a clone that skipped
    # `git submodule update --init`) -- mirrors the equivalent guard for
    # third_party/SDL/SDL_image/SDL_mixer in cmake/ThirdPartySDL.cmake.
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/googletest/CMakeLists.txt")
        message(FATAL_ERROR
            "Missing vendored 'googletest' in ${CMAKE_CURRENT_SOURCE_DIR}/vendor. "
            "Run: git submodule update --init "
            "(a plain ZIP/tarball export of this repository cannot contain "
            "submodule content -- clone with git and initialize submodules instead)")
    endif()

    add_subdirectory(vendor/googletest)

    enable_testing()

    # Physical module layout: module-owned tests live in modules/<name>/tests (and
    # modules/renderers/<family>/tests), each preserving its former tests/-relative mirror
    # path -- which is why every path-tail filter below keeps matching unchanged. The
    # top-level tests/ tree retains only the shared fixture assets and the cross-module
    # minimal-link probes.
    file(GLOB_RECURSE CNA_TEST_SOURCES CONFIGURE_DEPENDS
            "modules/*/tests/*.cpp"
            "modules/renderers/*/tests/*.cpp"
            "tests/*.cpp"
    )

    # HTMLDOM-123: these two files are complete standalone Win32 programs used only by the
    # dedicated GLIDE ABI DLL/loader targets in cmake/Tests/GlideTests.cmake.  They are not GTest
    # translation units: one exports a DLL and the other defines its own main().  Letting the
    # recursive test glob add them to CnaTests breaks every non-Windows configuration (including
    # HTML_DOM/Emscripten) at <windows.h>, and would duplicate main in a Windows CnaTests link.
    list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX
        ".*/CNA/Internal/Renderers/Glide/(FakeGlide3xDll|GlideAbiLoaderTests)\\.cpp$")

    # The minimal-link module probes (tests/modules/*.cpp, cmake/Tests/ModuleProbes.cmake) are
    # standalone executables with their own main(), not GTest translation units -- same reason
    # as the Glide ABI programs just above.
    list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/tests/modules/.*\\.cpp$")

    # plans/plan_binding.md CBIND-033: the C API module owns its own test executables
    # (modules/c-api/CMakeLists.txt) and none of its C++ translation units is a GTest unit --
    # HandleRegistryTest.cpp and BoundaryDetailTest.cpp define their own main(), and
    # AbiHeaderCpp.cpp is a pure ABI compile check linked into cna_c_api_abi_smoke. Letting the
    # recursive glob add them to CnaTests fails the build at <CNA/C/abi.h> (the C API's include
    # path is scoped to its own targets) and would duplicate main -- same reason as the Glide ABI
    # programs and the module probes above.
    list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/modules/c-api/tests/.*\\.cpp$")

    # plans/plan_apple.md APPLE-11: the Apple smoke application (cmake/AppleSmoke.cmake) is a complete
    # program with its own main(), for the same reason as the two entries above. Swept into
    # CnaTests it does not merely add a case -- its main() replaces GTest's, so the test binary
    # links, runs the smoke app and exits without running a single test.
    list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/tests/apple/.*\\.cpp$")

    # plans/plan_platform.md PLAT-119: the platform module's SDL3-specific tests exercise
    # CNA::Platform::Sdl3, which is compiled only when CNA_PLATFORM=SDL3. Under any other
    # platform selection they would reference symbols that do not exist and fail to link -- found
    # by actually configuring CNA_PLATFORM=HEADLESS rather than by inspection. The
    # implementation-neutral tests (the contract, and the conformance suite parameterised over
    # every available implementation) always build.
    if(NOT CNA_PLATFORM STREQUAL "SDL3")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/modules/platform/tests/.*/Sdl3.*\\.cpp$")
    endif()
    # SDL2 implementation tests include SDL2's real event structure and are meaningful only when
    # the SDL2 backend was selected. Contract tests above remain parameterised over all builds.
    if(NOT CNA_PLATFORM STREQUAL "SDL2")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/modules/platform/tests/.*/Sdl2.*\\.cpp$")
    endif()
    # The SDL2 native-queue mapper test must not share CnaTests with SDL3-native fixture tests:
    # SDL deliberately makes both imported targets declare mutually exclusive SDL_VERSION
    # interface requirements. It receives its own small executable below.
    if(CNA_PLATFORM STREQUAL "SDL2")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX
            ".*/modules/platform/tests/.*/Sdl2PlatformTests\\.cpp$")
    endif()

    # plans/plan_platform.md PLAT-94: like the general platform implementation above, the native
    # SDL3 audio test directly exercises a private implementation compiled only for its selected
    # audio platform. Contract/selection tests remain implementation-neutral.
    if(NOT CNA_AUDIO_PLATFORM STREQUAL "SDL3")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX
            ".*/modules/audio/tests/.*/Sdl3AudioDeviceTests\\.cpp$")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX
            ".*/modules/audio/tests/.*/Sdl3AudioRecordingDeviceTests\\.cpp$")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX
            ".*/modules/audio/tests/.*/AudioMixerPlatformTests\\.cpp$")
        # These suites exercise the SDL3_mixer implementation itself, not the portable XNA
        # facade.  The implementation is purposefully absent from SDL2/NULL link graphs.
        #
        # plans/plan_platform.md PLAT-SDL2-8 (2026-08-17): AudioCategoryTests and WaveBankTests were
        # missing from this list. They assert on real XACT playback -- `cue->getIsPlayingProperty()`
        # is true after Play(), instance limits evict a *playing* cue, a wave-bank entry decodes to
        # an exact frame count -- all of which need the engine that this branch just excluded, so
        # they failed 12 times over. Invisible until now because no configuration ever ran the full
        # suite with a non-SDL3 audio selection: the plan's own HEADLESS regression runs kept the
        # default SDL3 audio, and the CI cell that does select NULL audio builds CnaTests without
        # running it. Both gaps are closed alongside this line.
        foreach(_cna_sdl3_mixer_test IN ITEMS
                AudioCategoryTests
                AudioMixerTests
                CueTests
                DynamicSoundEffectInstanceTests
                OfflineAudioRendererTests
                SoundBankTests
                SoundEffectInstanceTests
                SoundEffectTests
                WaveBankTests)
            list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX
                ".*/modules/audio/tests/.*/${_cna_sdl3_mixer_test}\\.cpp$")
        endforeach()
    endif()
    if(NOT CNA_AUDIO_PLATFORM STREQUAL "SDL2")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX
            ".*/modules/audio/tests/.*/Sdl2AudioDeviceTests\\.cpp$")
    else()
        # SDL2 and SDL3 deliberately publish incompatible interface requirements.  The general
        # CnaTests binary retains SDL3-native renderer fixtures, so keep the native SDL2 audio
        # fixture in its own executable just like the SDL2 platform-event fixture above.
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX
            ".*/modules/audio/tests/.*/Sdl2AudioDeviceTests\\.cpp$")
    endif()

    # plans/plan_platform.md PLAT-130: TerminalPlatform is built on termios and pseudo-terminals, so
    # both it and its tests are POSIX-only -- excluded on Windows for the same reason the
    # implementation directory is (modules/platform/CMakeLists.txt), not gated line-by-line.
    # Everywhere else they always build, whatever CNA_PLATFORM says, because the implementation
    # always does.
    if(WIN32)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/modules/platform/tests/.*/Terminal.*\\.cpp$")
    endif()

    # MEDIA-233: Video/VideoPlayer and the XNB reader now exist in every build. Only suites that
    # require successful decoding of real fixture bytes are excluded without the optional backend;
    # VideoBackendAvailabilityTests and the reader/containment suites verify the no-backend contract.
    if(NOT CNA_FFMPEG_AVAILABLE)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Media/VideoDecoderTests\\.cpp$")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/Media/Video/VideoTests\\.cpp$")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/Media/Video/VideoPlayerTests\\.cpp$")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/Content/ContentManagerVideoXnbTests\\.cpp$")
    endif()

    # TwoProcessLoopbackTest.cpp (Task 6.1) uses POSIX-only process APIs (posix_spawn, poll,
    # sys/wait.h) to orchestrate two independent OS processes. Compiles under mingw-w64 and
    # Emscripten (both provide POSIX-ish libc stubs) but can't actually work under either: no real
    # multi-process spawning exists in a single Node.js/Wasm module. Also excluded on Android: the
    # harness path baked in via CNA_NET_HARNESS_PATH is an absolute path on the build machine, not
    # the on-device filesystem, and CnaTests is run as a bare pushed executable, not a packaged app
    # with its own bundled assets. iOS (plans/plan_apple.md APPLE-5) is excluded for both of those
    # reasons at once: an app-sandboxed process may not spawn another executable at all, and the
    # baked-in build-machine harness path does not exist inside the .app either. Not part of the Task 6.2/6.4 verification filters
    # (*Network*:*Gamer*:*ENet*:*Packet*) either, since its suite name is TwoProcessLoopbackTest.
    # plans/plan_dx1.md DX1-88 regression pass: found and fixed a pre-existing, not-DX1-specific gap --
    # this glob picks up ENet-specific tests unconditionally, but those files include
    # <enet/enet.h> directly, which only resolves when CNA_ENABLE_NET actually configured
    # the vendored ENet target (cmake/ThirdPartyENet.cmake). CNA_ENABLE_NET=OFF is a real,
    # supported configuration (CMakeLists.txt's own option default is ON, but OFF is explicitly
    # supported for builds that don't need Net/GamerServices) -- this was simply never exercised
    # against a from-scratch full CnaTests build before. Excluded the same way the WIN32/
    # EMSCRIPTEN/ANDROID-specific files just below already are.
    if(NOT CNA_ENABLE_NET)
        # REMED-BUILD-019: NET=OFF omits both CNA_Net and CNA_GamerServices, so every test whose
        # implementation lives in either disabled optional module must also leave CnaTests. This
        # exact root-based boundary includes direct and transitive ENet-header consumers without
        # suppressing any test outside the two disabled modules.
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Net/.*\\.cpp$")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/Microsoft/Xna/Framework/Net/.*\\.cpp$")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/Microsoft/Xna/Framework/GamerServices/.*\\.cpp$")

        # Keep the source-list contract executable: future glob/filter changes must fail at
        # configure time rather than compiling a disabled-module test into an un-linkable target.
        foreach(_cna_disabled_net_test IN LISTS CNA_TEST_SOURCES)
            if(_cna_disabled_net_test MATCHES "/(CNA/Internal/Net|Microsoft/Xna/Framework/(Net|GamerServices))/")
                message(FATAL_ERROR "NET=OFF left disabled-module test selected: ${_cna_disabled_net_test}")
            endif()
        endforeach()
        unset(_cna_disabled_net_test)
    endif()

    if(WIN32 OR EMSCRIPTEN OR ANDROID OR CNA_APPLE_IOS)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Net/TwoProcessLoopbackTest\\.cpp$")
        # GamerServicesDispatcherHangRegressionTest.cpp (Task 12.1) uses the same POSIX-only
        # posix_spawn/sys-wait process APIs, for the same reasons — see TwoProcessLoopbackTest's
        # own exclusion comment just above.
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Net/GamerServicesDispatcherHangRegressionTest\\.cpp$")
    endif()

    # AudioMixerTests.cpp (Task P9-HARDWARE-005) uses the same POSIX-only process APIs
    # (posix_spawn, poll, sys/wait.h) to spawn tools/audio/audio_no_hardware_harness.cpp as an
    # independent OS process, for the same reason TwoProcessLoopbackTest.cpp needs one above.
    # Excluded on the same platforms and for the same reasons (no real process spawning in a
    # single Node.js/Wasm module; CNA_AUDIO_NO_HARDWARE_HARNESS_PATH is a build-machine absolute
    # path, meaningless on-device on Android).
    if(WIN32 OR EMSCRIPTEN OR ANDROID OR CNA_APPLE_IOS)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Audio/AudioMixerTests\\.cpp$")
    endif()

    # GltfToCnjToolTests.cpp (plans/plan_cnj.md CNB-52) uses the same POSIX-only posix_spawn/sys-wait
    # process APIs to spawn cna_tool_gltf_to_cnj as an independent OS process, for the same
    # reasons as the harness-spawning tests above.
    if(WIN32 OR EMSCRIPTEN OR ANDROID OR CNA_APPLE_IOS)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/Microsoft/Xna/Framework/Content/GltfToCnjToolTests\\.cpp$")
        # plans/plan_cnb.md CNBF-064: CnbCompilerToolTests.cpp spawns cna_tool_cnj_to_cnb with the
        # same POSIX-only posix_spawn/sys-wait APIs, so it is excluded on exactly the same
        # platforms and for exactly the same reasons.
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Content/Cnb/CnbCompilerToolTests\\.cpp$")
        # CnbModelEquivalenceTests.cpp spawns cna_tool_gltf_to_cnj the same way, for the same
        # reason: it needs a REAL converted asset, not a hand-built approximation of one.
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Content/Cnb/CnbModelEquivalenceTests\\.cpp$")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Content/Cnb/CnbInfoToolTests\\.cpp$")
        # plans/plan_cnb.md CNBF-120: CnbSourceToolTests.cpp spawns cna_tool_source_to_cnb the same
        # way, so it is excluded on exactly the same platforms and for exactly the same reasons.
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Content/Cnb/CnbSourceToolTests\\.cpp$")
        # plans/plan_content_pipeline.md CP-006: this test spawns cna-content to exercise its real
        # filesystem publication and process exit contract.
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Content/Pipeline/ContentPipelineCliTests\\.cpp$")
    endif()

    # DevicesShutdownOrderingTests.cpp (Task SDLCORE-011) uses the same POSIX-only process APIs
    # (posix_spawn, poll, sys/wait.h) to spawn tools/devices/shutdown_ordering_harness.cpp as an
    # independent OS process, for the same reason TwoProcessLoopbackTest.cpp needs one above.
    # Excluded on the same platforms and for the same reasons.
    # plans/plan_platform.md PLAT-SDL2-6: also excluded for an SDL2-only selection, where its harness is
    # not built at all. That harness exists to call the real SDL3 SDL_Quit() at process teardown,
    # which is an ordering hazard this selection cannot reach -- and building it would put SDL3
    # back on a link line the selection is defined by not having (cmake/Sdl2OnlyConfiguration.cmake).
    if(WIN32 OR EMSCRIPTEN OR ANDROID OR CNA_APPLE_IOS OR CNA_SDL2_ONLY_CONFIGURATION)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/Microsoft/Devices/Detail/DevicesShutdownOrderingTests\\.cpp$")
    endif()



    # plans/plan_fna3d.md: the FNA3D renderer's own GTest suites live under
    # modules/renderers/fna3d/tests/ and include the renderer's headers, which resolve only when
    # the FNA3D renderer is configured (the FNA3D/MojoShader include roots come with the renderer
    # target). Excluded from every other renderer's corpus by the same convention as the Wicked
    # and Magnum directories above; under FNA3D the corpus keeps them.
    if(NOT "FNA3D" IN_LIST CNA_RENDERER_IDENTITIES)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Renderers/Fna3d/.*\\.cpp$")
    endif()

    # plan/plan_compilation.md COMP-002: compile each module's test sources through one object library.
    # The legacy CnaTests executable consumes every group, preserving its complete test inventory,
    # while focused executables consume one group and avoid compiling unrelated test translation
    # units. Object libraries are intentional: ordinary static archives can discard GoogleTest's
    # self-registering objects when no symbol is referenced directly.
    add_library(cna_test_build_config INTERFACE)
    target_link_libraries(cna_test_build_config INTERFACE
        SHARP_RUNTIME
        gtest)

    # CnaTests historically compiled through the CNA umbrella and therefore saw every public
    # module include root. A few contract tests intentionally include a second subsystem's public
    # header without calling into that subsystem. Preserve that compile-only visibility without
    # linking/building every module into every focused executable.
    foreach(_cna_test_include_module IN ITEMS
            audio content core devices devices-ext gamer-services graphics graphics-ext input math
            media net platform runtime storage)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/${_cna_test_include_module}/include")
            target_include_directories(cna_test_build_config INTERFACE
                "${CMAKE_CURRENT_SOURCE_DIR}/modules/${_cna_test_include_module}/include")
        endif()
    endforeach()

    # The focused executables link only their owning module's public dependency closure. The
    # renderer-policy and cross-module groups retain the CNA umbrella because their purpose is to
    # exercise the aggregate. This makes a clean CnaMathTests/CnaCoreTests build genuinely focused,
    # rather than merely omitting unrelated test sources while still building every CNA module.
    set(CNA_TEST_GROUP_DEPENDENCY_audio cna_audio cna_input cna_media)
    # The content group includes GltfMaterialToPbrMaterialTests, whose contract deliberately
    # crosses into the engine-layer material bridge implemented by cna_graphics_ext. Compile-only
    # include visibility is insufficient: the focused executable must link that implementation.
    set(CNA_TEST_GROUP_DEPENDENCY_content cna_content cna_graphics_ext)
    # plans/plan_xnapipeline.md XNAP-90: cna_content_pipeline is the build-time-only module (the
    # FreeType-backed .spritefont route). It is deliberately absent from the CNA runtime umbrella,
    # so this group names it explicitly and CnaTests links it separately below.
    # XNAP-A5: cna_content_compiler carries RunContentCompiler(), which the .fx product-route
    # tests drive directly rather than through a manually assembled registry. Both are build-time
    # libraries; neither is in a game's link closure, which XnbRuntimeDependencyBoundaryTest
    # asserts rather than assumes.
    set(CNA_TEST_GROUP_DEPENDENCY_content_pipeline cna_content_pipeline cna_content_compiler)
    set(CNA_TEST_GROUP_DEPENDENCY_core cna_core)
    set(CNA_TEST_GROUP_DEPENDENCY_devices cna_devices)
    set(CNA_TEST_GROUP_DEPENDENCY_devices_ext cna_devices_ext)
    set(CNA_TEST_GROUP_DEPENDENCY_gamer_services CNA_GamerServices)
    set(CNA_TEST_GROUP_DEPENDENCY_graphics cna_graphics_core)
    set(CNA_TEST_GROUP_DEPENDENCY_graphics_ext cna_graphics_ext)
    set(CNA_TEST_GROUP_DEPENDENCY_input cna_input)
    set(CNA_TEST_GROUP_DEPENDENCY_integration CNA)
    set(CNA_TEST_GROUP_DEPENDENCY_math cna_math)
    set(CNA_TEST_GROUP_DEPENDENCY_media cna_media)
    set(CNA_TEST_GROUP_DEPENDENCY_net CNA_Net)
    set(CNA_TEST_GROUP_DEPENDENCY_platform cna_platform)
    set(CNA_TEST_GROUP_DEPENDENCY_renderers CNA)
    set(CNA_TEST_GROUP_DEPENDENCY_runtime cna_runtime)
    set(CNA_TEST_GROUP_DEPENDENCY_storage cna_storage)

    set(CNA_TEST_FOCUSED_TARGET_audio CnaAudioTests)
    set(CNA_TEST_FOCUSED_TARGET_content CnaContentTests)
    set(CNA_TEST_FOCUSED_TARGET_content_pipeline CnaContentPipelineTests)
    set(CNA_TEST_FOCUSED_TARGET_core CnaCoreTests)
    set(CNA_TEST_FOCUSED_TARGET_devices CnaDevicesTests)
    set(CNA_TEST_FOCUSED_TARGET_devices_ext CnaDevicesExtTests)
    set(CNA_TEST_FOCUSED_TARGET_gamer_services CnaGamerServicesTests)
    set(CNA_TEST_FOCUSED_TARGET_graphics CnaGraphicsTests)
    set(CNA_TEST_FOCUSED_TARGET_graphics_ext CnaGraphicsExtTests)
    set(CNA_TEST_FOCUSED_TARGET_input CnaInputModuleTests)
    set(CNA_TEST_FOCUSED_TARGET_integration CnaIntegrationTests)
    set(CNA_TEST_FOCUSED_TARGET_math CnaMathTests)
    set(CNA_TEST_FOCUSED_TARGET_media CnaMediaTests)
    set(CNA_TEST_FOCUSED_TARGET_net CnaNetTests)
    set(CNA_TEST_FOCUSED_TARGET_platform CnaPlatformModuleTests)
    set(CNA_TEST_FOCUSED_TARGET_renderers CnaRendererTests)
    set(CNA_TEST_FOCUSED_TARGET_runtime CnaRuntimeTests)
    set(CNA_TEST_FOCUSED_TARGET_storage CnaStorageTests)

    set(_cna_test_groups)
    foreach(_cna_test_source IN LISTS CNA_TEST_SOURCES)
        if(_cna_test_source MATCHES "/modules/renderers/")
            set(_cna_test_group renderers)
        elseif(_cna_test_source MATCHES "/modules/([^/]+)/tests/")
            set(_cna_test_group "${CMAKE_MATCH_1}")
            string(REPLACE "-" "_" _cna_test_group "${_cna_test_group}")
        else()
            set(_cna_test_group integration)
        endif()
        list(APPEND "CNA_TEST_GROUP_${_cna_test_group}" "${_cna_test_source}")
        list(APPEND _cna_test_groups "${_cna_test_group}")
    endforeach()
    list(REMOVE_DUPLICATES _cna_test_groups)
    list(SORT _cna_test_groups)

    add_executable(CnaTests)
    set(CNA_FOCUSED_TEST_TARGETS)
    foreach(_cna_test_group IN LISTS _cna_test_groups)
        set(_cna_test_object_target "cna_${_cna_test_group}_test_objects")
        set(_cna_focused_test_target "${CNA_TEST_FOCUSED_TARGET_${_cna_test_group}}")
        set(_cna_test_group_dependencies ${CNA_TEST_GROUP_DEPENDENCY_${_cna_test_group}})
        if(NOT _cna_focused_test_target)
            message(FATAL_ERROR
                "No focused test target name is defined for group '${_cna_test_group}'.")
        endif()
        foreach(_cna_test_group_dependency IN LISTS _cna_test_group_dependencies)
            if(NOT TARGET ${_cna_test_group_dependency})
                message(FATAL_ERROR
                    "Test group '${_cna_test_group}' requires missing target "
                    "'${_cna_test_group_dependency}'.")
            endif()
        endforeach()

        add_library(${_cna_test_object_target} OBJECT EXCLUDE_FROM_ALL
            ${CNA_TEST_GROUP_${_cna_test_group}})
        target_link_libraries(${_cna_test_object_target} PRIVATE
            cna_test_build_config
            ${_cna_test_group_dependencies})
        if(CNA_ENABLE_PCH AND _cna_test_group STREQUAL "content")
            # COMP-003 deliberately pilots only stable standard-library and GoogleTest headers.
            # CNA public headers stay textual so editing the framework API does not rebuild a
            # large project-owned PCH before every content test translation unit can proceed.
            target_precompile_headers(${_cna_test_object_target} PRIVATE
                <algorithm>
                <array>
                <cstdint>
                <filesystem>
                <memory>
                <optional>
                <span>
                <string>
                <string_view>
                <unordered_map>
                <utility>
                <vector>
                <gtest/gtest.h>)

            # Safe ccache support for PCH requires relaxed pch_defines/time_macros correctness
            # checks. CNA does not impose those global tradeoffs, so only this experimental object
            # target bypasses the inherited launcher; every dependency remains cacheable normally.
            if(CNA_USE_CCACHE AND CNA_CCACHE_PROGRAM)
                set_property(TARGET ${_cna_test_object_target} PROPERTY CXX_COMPILER_LAUNCHER "")
                message(STATUS
                    "CNA: content-test PCH bypasses ccache; no unsafe sloppiness was enabled")
            endif()
        endif()
        target_sources(CnaTests PRIVATE "$<TARGET_OBJECTS:${_cna_test_object_target}>")
        set("CNA_TEST_OBJECT_TARGET_${_cna_test_group}" "${_cna_test_object_target}")

        # Focused executables are developer iteration targets, not additional CTest registrations;
        # the full CnaTests discovery remains the single default suite and is therefore not run
        # twice in CI.
        add_executable(${_cna_focused_test_target} EXCLUDE_FROM_ALL
            "$<TARGET_OBJECTS:${_cna_test_object_target}>")
        target_link_libraries(${_cna_focused_test_target} PRIVATE
            cna_test_build_config
            ${_cna_test_group_dependencies}
            gtest_main)
        list(APPEND CNA_FOCUSED_TEST_TARGETS "${_cna_focused_test_target}")
    endforeach()
    message(STATUS "CNA: focused unit-test targets: ${CNA_FOCUSED_TEST_TARGETS}")

    target_link_libraries(CnaTests PRIVATE
        cna_test_build_config
        CNA
        gtest_main)

    # The build-time-only content-pipeline module is not part of the CNA runtime umbrella by
    # design (a game must not link FreeType because the content pipeline exists). CnaTests
    # consumes every group's objects, so it needs that one library named explicitly.
    if(TARGET cna_content_pipeline)
        target_link_libraries(CnaTests PRIVATE cna_content_pipeline)
    endif()
    if(TARGET cna_content_compiler)
        target_link_libraries(CnaTests PRIVATE cna_content_compiler)
    endif()

    # mingw-w64's <cmath> only exposes M_PI when _USE_MATH_DEFINES is set (unlike glibc, which
    # defines it unconditionally) — a handful of test files use M_PI directly as a reference value.
    if(MINGW)
        target_compile_definitions(cna_test_build_config INTERFACE _USE_MATH_DEFINES)
        # Known mingw-w64/GCC PE-COFF toolchain limitation: std::type_info::operator== is emitted
        # as vague linkage (COMDAT) in libstdc++.a, but a test binary this large (many RTTI-using
        # TUs) trips a case where the linker fails to fold it against the identical libstdc++
        # definition. Both copies are byte-identical, so allowing the duplicate is safe.
        target_link_options(CnaTests PRIVATE -Wl,--allow-multiple-definition)
    endif()

    if(EMSCRIPTEN)
        # Emscripten's EXIT_RUNTIME defaults to 0 (deliberately keeps the JS runtime alive after
        # main() returns, so pending async JS/WebSocket handles can keep running) - without this,
        # `node --experimental-wasm-stack-switching CnaTests.js` never exits after printing its
        # results, even on success.
        target_link_options(CnaTests PRIVATE -sEXIT_RUNTIME=1)

        # Emscripten's default build is fully synchronous/single-threaded: a real WebSocket
        # handshake structurally cannot complete while C++ code holds the call stack (confirmed
        # empirically - even a real 1s sleep loop never lets it finish, since nothing returns
        # control to Node's event loop). The SystemLink loopback tests need emscripten_sleep() to
        # actually yield back to Node between polls. JSPI provides that suspension while remaining
        # compatible with the Wasm exception ABI selected at the repository root; Emscripten 6
        # explicitly rejects Asyncify mixed with -fwasm-exceptions. Scoped to CnaTests only (a
        # per-executable link setting - it does not affect native/Windows builds or any other
        # Emscripten executable target such as the demos or the two-process harness).
        target_link_options(CnaTests PRIVATE -sJSPI=1)
    endif()

    # plans/plan_platform.md PLAT-SDL2-6: SDL3 is a test-fixture dependency here (native renderer and
    # audio fixtures), not a framework one -- CNA's own modules link their platform library
    # privately and conditionally since PLAT-122. Under an SDL2-only selection every source that
    # needs SDL3 has already been filtered out above, and linking it anyway would put two
    # libraries exporting the same entry points into one process; see
    # cmake/Sdl2OnlyConfiguration.cmake for why that silently invalidates the conformance run.
    if(NOT CNA_SDL2_ONLY_CONFIGURATION)
        target_link_libraries(cna_test_build_config INTERFACE SDL3::SDL3)
    endif()

    # The Draco corpus owns one test-only encoder oracle: it recreates the committed compressed
    # streams with CNA's pinned dependency and byte-compares them, so the otherwise standard-
    # library-only Python generator never needs to execute a native tool. Production content code
    # keeps Draco PRIVATE; expose it only to CnaTests when that optional decoder is configured.
    if(CNA_DRACO_AVAILABLE)
        target_link_libraries(${CNA_TEST_OBJECT_TARGET_content} PRIVATE cna_draco)
        target_link_libraries(CnaContentTests PRIVATE cna_draco)
        target_link_libraries(CnaTests PRIVATE cna_draco)
        if(NOT CNA_USE_SYSTEM_DRACO)
            target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE CNA_VENDORED_DRACO)
        endif()
    endif()

    if(CNA_PLATFORM STREQUAL "SDL2")
        add_executable(cna_platform_sdl2_tests
            modules/platform/tests/CNA/Platform/Sdl2PlatformTests.cpp)
        target_link_libraries(cna_platform_sdl2_tests PRIVATE
            cna_platform
            SDL2::SDL2
            gtest_main)
        add_test(NAME CnaSdl2PlatformTests COMMAND cna_platform_sdl2_tests)
        set_tests_properties(CnaSdl2PlatformTests PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endif()

    if(CNA_AUDIO_PLATFORM STREQUAL "SDL2")
        add_executable(cna_audio_sdl2_tests
            modules/audio/tests/CNA/Audio/Platform/Sdl2AudioDeviceTests.cpp)
        target_include_directories(cna_audio_sdl2_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/audio/src)
        target_link_libraries(cna_audio_sdl2_tests PRIVATE
            cna_audio
            SDL2::SDL2
            gtest_main)
        add_test(NAME CnaSdl2AudioDeviceTests COMMAND cna_audio_sdl2_tests)
        set_tests_properties(CnaSdl2AudioDeviceTests PROPERTIES
            ENVIRONMENT "SDL_AUDIODRIVER=dummy")
    endif()

    # The metal and glide policy suites deliberately compile on every renderer (see their own
    # header comments); their policy headers ride the unconditional header-interface targets
    # those renderer modules always define.
    target_link_libraries(cna_test_build_config INTERFACE
        cna_renderer_metal_headers
        cna_renderer_glide_headers)

    # GltfImportCoreTests.cpp includes CNA/Internal/GltfImport/GltfImportCore.hpp directly (to call
    # ExtractMesh() without spawning the CLI tool), which itself includes cgltf.h -- CNA's own
    # target_include_directories for that path is PRIVATE (see cmake/CnaLibrary.cmake), so it does
    # not propagate to CnaTests via target_link_libraries and must be added here too.
    target_include_directories(cna_test_build_config INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
            # plans/plan_runtimerenderer.md RTR-P9-2: shared test-support headers reached by their
            # namespace path (CNA/RendererTestGate.hpp), matching how every module's public headers
            # are spelled.
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/graphics/tests
            # plans/plan_platform.md PLAT-77/PLAT-90: modules/platform/tests holds shared test
            # scaffolding (PlatformTestDecorator.hpp) that tests in other modules include to drive
            # CNA against a controlled platform. It replaces the eight per-subsystem injectable
            # backend seams modules/input used to carry for the same purpose, so it has to be
            # reachable from outside its own module. Deliberately NOT in any module's public
            # include tree -- a production build has no business including scaffolding.
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/platform/tests
            # PLAT-66: shared SDL-free coordinate-transform renderer used by the input bridge and
            # public Mouse tests. Test-only for the same reason as the platform fixtures above.
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/input/tests
            # PLAT-94: implementation test only; Sdl3AudioDevice remains out of public headers.
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/audio/src
            # plans/plan_cnb.md CNBF-122: the content tools' shared header-only helpers
            # (CnaToolAtomicWrite.hpp), so the atomic-write contract can be asserted directly
            # rather than only through what a subprocess happens to leave on disk.
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/common
    )

    # plans/plan_fx.md FX-060: the shared compiled-effect conformance suite is header-only test support
    # (fixtures plus the contract assertions every CompiledEffects backend must satisfy). It lives
    # under the top-level tests/ tree because it belongs to no single renderer module -- the point
    # is that a new backend runs the identical contract with only its own device setup.
    target_include_directories(cna_test_build_config INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
    )

    # plans/plan_runtimerenderer.md RTR-P9-9: one CNA_RENDERER_PRESENT_<IDENTITY> define per renderer
    # COMPILED INTO THIS BUILD, on the test executable only.
    #
    # A renderer's own test suite guards its body on `#if defined(CNA_RENDERER_<X>)`, and only the
    # DEFAULT renderer's macro is defined project-wide (that is what keeps every other renderer-gated
    # test in the corpus unambiguous). So in a multi-renderer build those files compiled to nothing:
    # the sources were there, the tests were not. Keeping their sources without this define was the
    # empty half of the fix.
    #
    # PRESENT_ says "compiled in", never "selected", so it cannot be confused with the identity
    # macros and does not disturb the exactly-one invariant GraphicsRendererCompileDefinitionTests
    # asserts.
    foreach(_cna_present_identity IN LISTS CNA_RENDERER_IDENTITIES)
        target_compile_definitions(cna_test_build_config INTERFACE
            "CNA_RENDERER_PRESENT_${_cna_present_identity}")
    endforeach()

    # ...and each compiled-in renderer's own public include root, for the same reason: those suites
    # include their renderer's headers ("CNA/Internal/Renderers/Magnum/MagnumBuffers.hpp"), and only
    # the DEFAULT renderer's target contributes its include directory to this executable. Enabling
    # the define without the include root just moves the failure from "no tests" to "no such file".
    foreach(_cna_present_dir IN LISTS CNA_RENDERER_DIRS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_cna_present_dir}/include")
            target_include_directories(cna_test_build_config INTERFACE
                "${CMAKE_CURRENT_SOURCE_DIR}/${_cna_present_dir}/include")
        endif()
    endforeach()

    # plans/plan_runtimerenderer.md RTR-P9-9, second half: a module's own include root is not enough when
    # its PUBLIC headers include a third-party library's headers in turn. LLGL is the case that
    # showed it -- modules/renderers/llgl/include/.../LlglSdlSurface.hpp opens with
    # `#include <LLGL/Surface.h>`, and LLGL's include directory reaches the llgl module through the
    # LLGL target it links, not through any directory of CNA's own. Adding only CNA's root moved the
    # failure from "no tests" to "LLGL/Surface.h: No such file or directory" in a multi build
    # containing LLGL.
    #
    # Giving this executable each present renderer target's OWN include list keeps that generic:
    # whatever a renderer module compiles against, its device-free suites can compile against too,
    # without this file having to know which third-party library each family uses.
    foreach(_cna_present_target IN LISTS CNA_RENDERER_TARGETS)
        if(TARGET ${_cna_present_target})
            target_include_directories(cna_test_build_config INTERFACE
                "$<TARGET_PROPERTY:${_cna_present_target},INCLUDE_DIRECTORIES>")
        endif()
    endforeach()

    # COMP-001 full-build regression: EasyGL's compiled-effect public headers include
    # mojoshader.h. The focused object groups copy renderer include directories above, but include
    # directories alone do not carry MojoShader's required public compile definitions (notably
    # MOJOSHADER_NO_VERSION_INCLUDE for its ungenerated source-tree version header).
    if(CNA_EASYGL_COMPILED_EFFECTS AND TARGET cna_mojoshader)
        target_link_libraries(cna_test_build_config INTERFACE cna_mojoshader)
    endif()

    # REMED-GFX-054's WebGPU-only IndexBuffer regression opens native error scopes around the
    # public operation. CNA's renderer intentionally keeps wgpu-native PRIVATE, so expose it only
    # to this test executable in the WebGPU configuration.
    # plans/plan_runtimerenderer.md RTR-P10-12: FNA3D's suites include mojoshader.h, and that header
    # includes the GENERATED mojoshader_version.h unless MOJOSHADER_NO_VERSION_INCLUDE is defined.
    # cna_fna3d carries that switch for the renderer target, but RTR-P9-9 gave this executable the
    # present renderers' include PATHS only -- so CnaTests found the header and then failed on the
    # generated one it pulls in. The include root alone is not the whole surface a family's suites
    # compile against; its switches are part of it.
    if("FNA3D" IN_LIST CNA_RENDERER_IDENTITIES AND TARGET cna_fna3d)
        target_link_libraries(cna_test_build_config INTERFACE cna_fna3d)
    endif()

    if("WEBGPU" IN_LIST CNA_RENDERER_IDENTITIES)
        target_link_libraries(cna_test_build_config INTERFACE WebGPU::WebGPU)
    endif()



    if(CNA_ENABLE_NET)
        target_link_libraries(CnaTests PRIVATE CNA_GamerServices CNA_Net)
    endif()

    if(TARGET cna_net_two_process_harness)
        # The net test object group owns the subprocess path so both the focused executable and
        # the full CnaTests compatibility executable build the harness before compiling it.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_net} cna_net_two_process_harness)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_net} PRIVATE
            CNA_NET_HARNESS_PATH="$<TARGET_FILE:cna_net_two_process_harness>"
        )
    endif()

    if(TARGET cna_net_gamerservices_dispatcher_harness)
        # Same reasoning as cna_net_two_process_harness just above.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_net} cna_net_gamerservices_dispatcher_harness)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_net} PRIVATE
            CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH="$<TARGET_FILE:cna_net_gamerservices_dispatcher_harness>"
        )
    endif()

    if(TARGET cna_fake_effect_compiler)
        # plans/plan_xnapipeline.md XNAP-A5: EffectSourceCommandLineTests.cpp drives the real
        # `.fx` product route with this program as the compiler, so it needs its path baked in --
        # same reasoning as cna_net_two_process_harness above. Baked in unconditionally so the
        # tests are required rather than silently skipped: a route test that skips when the
        # repository supplies its own prerequisite proves nothing.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content_pipeline} cna_fake_effect_compiler)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content_pipeline} PRIVATE
            CNA_FAKE_EFFECT_COMPILER_PATH="$<TARGET_FILE:cna_fake_effect_compiler>"
        )
    endif()

    if(TARGET cna_audio_no_hardware_harness)
        # Same reasoning as cna_net_two_process_harness above, for AudioMixerTests.cpp.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_audio} cna_audio_no_hardware_harness)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_audio} PRIVATE
            CNA_AUDIO_NO_HARDWARE_HARNESS_PATH="$<TARGET_FILE:cna_audio_no_hardware_harness>"
        )
    endif()

    # plans/plan_cnb.md CNBF-123: the CNB producer-output invariant test reads the three producers'
    # own sources, so it needs the tools tree by absolute path rather than by guessing at the
    # working directory. Baked in so the check can never silently skip itself in a build-directory
    # run -- a source-level invariant that skips is an invariant nobody is enforcing.
    target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE
        CNA_TOOLS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools"
    )

    if(TARGET cna_tool_gltf_to_cnj)
        # plans/plan_cnj.md CNB-52: GltfToCnjToolTests.cpp spawns the real converter tool as a
        # subprocess (same reasoning as cna_net_two_process_harness above -- a separate
        # executable with its own main(), not a library call) and needs its real built path
        # baked in at compile time.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content} cna_tool_gltf_to_cnj)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE
            CNA_GLTF_TO_CNJ_TOOL_PATH="$<TARGET_FILE:cna_tool_gltf_to_cnj>"
        )
    endif()

    if(TARGET cna_tool_cnj_to_cnb)
        # plans/plan_cnb.md CNBF-063/CNBF-064: CnbCompilerToolTests.cpp spawns the real .cnj -> .cnb
        # compiler as a subprocess, for the same reason GltfToCnjToolTests.cpp spawns its own
        # tool -- proving cross-process determinism means two genuinely separate processes, not
        # two calls inside one.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content} cna_tool_cnj_to_cnb)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE
            CNA_CNJ_TO_CNB_TOOL_PATH="$<TARGET_FILE:cna_tool_cnj_to_cnb>"
        )
    endif()

    if(TARGET cna_tool_gltf_to_cnb)
        # plans/plan_cnb.md CNBF-106: CnbGltfDirectToolTests.cpp spawns the direct compiler and
        # compares its output against the two-step route byte for byte. Two separate processes,
        # for the same reason the suites above use them.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content} cna_tool_gltf_to_cnb)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE
            CNA_GLTF_TO_CNB_TOOL_PATH="$<TARGET_FILE:cna_tool_gltf_to_cnb>"
        )
    endif()

    if(TARGET cna_tool_source_to_cnb)
        # plans/plan_cnb.md CNBF-120: CnbSourceToolTests.cpp spawns the direct source compiler as a
        # subprocess, for the same reason the suites above do -- its contract is its exit code, its
        # stderr and the bytes it leaves on disk, none of which a library call exercises. It was the
        # only CNB tool with no such wiring, so the executable had never been run by a test at all.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content} cna_tool_source_to_cnb)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE
            CNA_SOURCE_TO_CNB_TOOL_PATH="$<TARGET_FILE:cna_tool_source_to_cnb>"
        )
    endif()

    if(TARGET cna_tool_xnb_interop_fixtures)
        # plans/plan_xnapipeline.md XNAP-30/XNAP-43: the committed CNA-generated XNB corpus is
        # regenerated by a test and byte-compared against what is in the tree, so a change in the
        # writer's output can never land unnoticed in files an XNA interoperability run depends on.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content} cna_tool_xnb_interop_fixtures)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE
            CNA_XNB_INTEROP_FIXTURE_TOOL_PATH="$<TARGET_FILE:cna_tool_xnb_interop_fixtures>"
        )
    endif()

    if(TARGET cna_content_tool)
        # plans/plan_content_pipeline.md CP-006: run the real unified front end as a subprocess.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content} cna_content_tool)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE
            CNA_CONTENT_TOOL_PATH="$<TARGET_FILE:cna_content_tool>"
        )

        # plans/plan_xnapipeline.md XNAP-44: the multi-worker determinism suite needs the same
        # real executable, in separate OS processes, and the `.fx` route's stand-in compiler with
        # it -- an effect build must take part in a parallel build like any other asset.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content_pipeline} cna_content_tool)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content_pipeline} PRIVATE
            CNA_CONTENT_TOOL_PATH="$<TARGET_FILE:cna_content_tool>"
        )

        # plans/plan_content_pipeline.md CP-014: prove the public CMake helper itself delegates to
        # the real CLI and preserves its logical/output tree and manifest. A target-platform tool
        # cannot execute on the host, so cross configurations compile the guarded test but do not
        # instantiate this native build fixture.
        if(COMMAND cna_add_content AND NOT CMAKE_CROSSCOMPILING)
            set(_cna_content_cmake_fixture_output
                "${CMAKE_CURRENT_BINARY_DIR}/content-pipeline-cmake-fixture")
            cna_add_content(
                TARGET cna_content_cmake_fixture
                SOURCE_DIR "${CNA_SOURCE_DIR}/tests/assets/content_pipeline_cmake"
                OUTPUT_DIR "${_cna_content_cmake_fixture_output}"
                CONFIG_FILE
                    "${CNA_SOURCE_DIR}/tests/assets/content_pipeline_cmake/pipeline-config.json"
                WORKERS 2
                QUIET
            )
            get_property(_cna_content_cmake_fixture_config
                TARGET cna_content_cmake_fixture PROPERTY CNA_CONTENT_CONFIG_FILE)
            get_property(_cna_content_cmake_fixture_workers
                TARGET cna_content_cmake_fixture PROPERTY CNA_CONTENT_WORKERS)
            if(NOT _cna_content_cmake_fixture_config STREQUAL
                    "${CNA_SOURCE_DIR}/tests/assets/content_pipeline_cmake/pipeline-config.json")
                message(FATAL_ERROR
                    "cna_add_content did not retain the normalized CONFIG_FILE")
            endif()
            if(NOT _cna_content_cmake_fixture_workers STREQUAL "2")
                message(FATAL_ERROR "cna_add_content did not retain WORKERS")
            endif()
            add_dependencies(${CNA_TEST_OBJECT_TARGET_content} cna_content_cmake_fixture)
            file(TO_CMAKE_PATH "${_cna_content_cmake_fixture_output}"
                _cna_content_cmake_fixture_output_definition)
            set_property(SOURCE
                "${CNA_SOURCE_DIR}/modules/content/tests/CNA/Content/Pipeline/ContentPipelineCMakeIntegrationTests.cpp"
                APPEND PROPERTY COMPILE_DEFINITIONS
                    "CNA_CONTENT_CMAKE_FIXTURE_OUTPUT=\"${_cna_content_cmake_fixture_output_definition}\"")
            unset(_cna_content_cmake_fixture_output_definition)
            unset(_cna_content_cmake_fixture_config)
            unset(_cna_content_cmake_fixture_workers)
            unset(_cna_content_cmake_fixture_output)

            if(TARGET cna_custom_content_compiler_example)
                set(_cna_custom_content_cmake_fixture_output
                    "${CMAKE_CURRENT_BINARY_DIR}/custom-content-pipeline-cmake-fixture")
                cna_add_content(
                    TARGET cna_custom_content_cmake_fixture
                    SOURCE_DIR
                        "${CNA_SOURCE_DIR}/tests/assets/content_pipeline_custom_cmake"
                    OUTPUT_DIR "${_cna_custom_content_cmake_fixture_output}"
                    CONFIG_FILE
                        "${CNA_SOURCE_DIR}/tests/assets/content_pipeline_custom_cmake/custom-config.json"
                    CONTENT_EXECUTABLE
                        "$<TARGET_FILE:cna_custom_content_compiler_example>"
                    WORKERS 2
                    QUIET
                )
                add_dependencies(cna_custom_content_cmake_fixture
                    cna_custom_content_compiler_example)
                add_dependencies(${CNA_TEST_OBJECT_TARGET_content}
                    cna_custom_content_cmake_fixture)
                file(TO_CMAKE_PATH "${_cna_custom_content_cmake_fixture_output}"
                    _cna_custom_content_cmake_fixture_output_definition)
                set_property(SOURCE
                    "${CNA_SOURCE_DIR}/modules/content/tests/CNA/Content/Pipeline/ContentPipelineCMakeIntegrationTests.cpp"
                    APPEND PROPERTY COMPILE_DEFINITIONS
                        "CNA_CUSTOM_CONTENT_CMAKE_FIXTURE_OUTPUT=\"${_cna_custom_content_cmake_fixture_output_definition}\"")
                unset(_cna_custom_content_cmake_fixture_output_definition)
                unset(_cna_custom_content_cmake_fixture_output)
            endif()
        endif()
    endif()

    if(TARGET cna_custom_content_compiler_example)
        # CP-022: exercise a separately linked user-owned compiler with both a custom route and
        # CNA built-ins through the same command coordinator as stock cna-content.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content}
            cna_custom_content_compiler_example)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE
            CNA_CUSTOM_CONTENT_COMPILER_PATH="$<TARGET_FILE:cna_custom_content_compiler_example>"
        )
    endif()

    if(TARGET cna_content_compiler)
        # The embedding API is implemented by the build-time compiler library rather than the
        # runtime content module. Both executables that consume the shared content test objects
        # therefore carry that exact implementation for its direct API contract tests.
        target_link_libraries(CnaContentTests PRIVATE cna_content_compiler)
        target_link_libraries(CnaTests PRIVATE cna_content_compiler)
    endif()

    if(TARGET cna_tool_cnb_info)
        # plans/plan_cnb.md CNBF-H013: CnbInfoToolTests.cpp spawns the inspector as a subprocess, for
        # the same reason the other tool suites do -- it has its own main() and its contract is its
        # stdout and its exit code, neither of which a library call exercises.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_content} cna_tool_cnb_info)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_content} PRIVATE
            CNA_CNB_INFO_TOOL_PATH="$<TARGET_FILE:cna_tool_cnb_info>"
        )
    endif()

    if(TARGET cna_devices_shutdown_ordering_harness)
        # Same reasoning as cna_net_two_process_harness above, for
        # DevicesShutdownOrderingTests.cpp (Task SDLCORE-011) -- calls the real SDL_Quit(),
        # which must not run inside the shared CnaTests process itself.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_devices} cna_devices_shutdown_ordering_harness)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_devices} PRIVATE
            CNA_DEVICES_SHUTDOWN_ORDERING_HARNESS_PATH="$<TARGET_FILE:cna_devices_shutdown_ordering_harness>"
        )
    endif()

    if(TARGET cna_platform_terminal_restoration_harness)
        # plans/plan_platform.md PLAT-131: same reasoning as cna_net_two_process_harness above, and more
        # sharply -- four of the five exit paths TerminalSession must restore on destroy the
        # process, so they cannot be asserted inside this binary at all.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_platform} cna_platform_terminal_restoration_harness)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_platform} PRIVATE
            CNA_PLATFORM_TERMINAL_RESTORATION_HARNESS_PATH="$<TARGET_FILE:cna_platform_terminal_restoration_harness>"
        )
    endif()

    if(TARGET cna_platform_terminal_resize_harness)
        # plans/plan_platform.md PLAT-136: see cna_platform_terminal_restoration_harness above.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_platform} cna_platform_terminal_resize_harness)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_platform} PRIVATE
            CNA_PLATFORM_TERMINAL_RESIZE_HARNESS_PATH="$<TARGET_FILE:cna_platform_terminal_resize_harness>"
        )
    endif()

    if(TARGET cna_audio_mixer_destroy_active_static_voice_harness)
        # Task AUD-04-008: same reasoning as cna_net_two_process_harness above.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_audio} cna_audio_mixer_destroy_active_static_voice_harness)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_audio} PRIVATE
            CNA_AUDIO_MIXER_DESTROY_ACTIVE_STATIC_VOICE_HARNESS_PATH="$<TARGET_FILE:cna_audio_mixer_destroy_active_static_voice_harness>"
        )
    endif()

    if(TARGET cna_audio_mixer_destroy_active_dynamic_voice_harness)
        # Task AUD-04-009: same reasoning as cna_net_two_process_harness above.
        add_dependencies(${CNA_TEST_OBJECT_TARGET_audio} cna_audio_mixer_destroy_active_dynamic_voice_harness)
        target_compile_definitions(${CNA_TEST_OBJECT_TARGET_audio} PRIVATE
            CNA_AUDIO_MIXER_DESTROY_ACTIVE_DYNAMIC_VOICE_HARNESS_PATH="$<TARGET_FILE:cna_audio_mixer_destroy_active_dynamic_voice_harness>"
        )
    endif()

    if(TARGET easy-gl)
        target_link_libraries(cna_test_build_config INTERFACE easy-gl)
    endif()

    # plans/plan_dx.md DX-15 follow-up + plans/plan_dx9.md D9-123 follow-up (merge-reconciled 2026-07-16):
    # now that CnaTests.exe genuinely builds under the D3D9/D3D11/D3D12/Direct2D MinGW cross-targets,
    # gtest_discover_tests(DISCOVERY_MODE PRE_TEST) below executes it directly to enumerate tests
    # -- and any add_test(COMMAND CnaTests ...) test (e.g. CnaInputTests, further below) does the
    # same at run time. All three need the same Wine wrapper DirectX9_Smoke/DirectX11_Smoke/DirectX12_Smoke
    # already use, or every ctest invocation in this build tree fails outright trying to exec a
    # Windows PE natively on the Linux host. *_SKIP_*_GATE=1 because a bare --gtest_list_tests (or
    # most individual unit tests) never creates a real graphics device, so the DXVK/vkd3d-presence
    # gate those wrappers normally enforce would misfire here. MUST be set before
    # gtest_discover_tests() below -- it reads this property immediately at configure time, not
    # lazily at generate/test time. `CMAKE_CROSSCOMPILING` is used as the outer guard (equivalent
    # to `MINGW` for every real build configuration this project uses, since these three renderers
    # are only ever built via the MinGW cross-toolchain) so a D3D9 branch could be added without
    # touching D3D11/D3D12's own already-working invocation style.
    if(CMAKE_CROSSCOMPILING)
        if(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX9")
            # D9-123: this renderer's own wrapper takes a simpler `env;VAR=1;script` form (no
            # `${CMAKE_COMMAND} -E env`/explicit `bash` prefix) -- proven end-to-end for D3D9
            # (`ctest -L D3D9` green) and for D3D11 when reused verbatim there too (Task 1106).
            set_target_properties(CnaTests PROPERTIES CROSSCOMPILING_EMULATOR
                "env;CNA_D3D9_SKIP_DXVK_GATE=1;${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk9.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX11")
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_D3D11_SKIP_DXVK_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-dxvk.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX12")
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_D3D12_SKIP_VKD3D_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-vkd3d.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX1")
            # plans/plan_dx2.md DX2-84 follow-up: this gap pre-dates DIRECTX2 (DIRECTX1 never got this wiring
            # either, plans/plan_dx1.md's own DX1-88 full-suite regression must have run CnaTests.exe
            # directly through run-wine-directx1.sh by hand rather than via `ctest -L DIRECTX1`'s
            # gtest_discover_tests(PRE_TEST) step) -- fixed here for both renderers together, same
            # skip-gate reasoning as D3D9/D3D11/D3D12 above (a bare --gtest_list_tests never opens
            # a real DirectDraw object).
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX1_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx1.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX2")
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX2_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx2.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX3")
            # plans/plan_dx3.md: wired proactively (not discovered by a from-scratch regression this
            # time) -- same DX2-84 finding/fix, applied up front for this new renderer.
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX3_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx3.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX5")
            # plans/plan_dx5.md: same proactive wiring as DIRECTX3.
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX5_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx5.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX6")
            # plans/plan_dx6.md: same proactive wiring as DIRECTX3/DIRECTX5.
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX6_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx6.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX7")
            # plans/plan_dx7.md: same proactive wiring as DIRECTX3/DIRECTX5/DIRECTX6.
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX7_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx7.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX8")
            # plans/plan_dx8.md: DXVK-delivered (not DirectDraw-based), same proactive wiring shape as
            # D3D9's own run-wine-dxvk9.sh gate -- CNA_DX8_SKIP_DXVK_GATE for a binary that
            # legitimately never opens a real D3D8 device (e.g. a bare --gtest_list_tests call).
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX8_SKIP_DXVK_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx8.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX10")
            # plans/plan_d3d10.md: DXVK-delivered (via d3d10core), same proactive wiring shape as DIRECTX8's
            # own gate -- CNA_D3D10_SKIP_DXVK_GATE for a binary that legitimately never opens a
            # real D3D10 device (e.g. a bare --gtest_list_tests call).
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_D3D10_SKIP_DXVK_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx10.sh")
        elseif("DIRECT2D" IN_LIST CNA_RENDERER_IDENTITIES)
            # Direct2D needs the normal/dedicated prefix selected by run-wine-direct2d.sh, not the
            # D3D11-only DXVK prefix (which may not contain Wine's d2d1 runtime). Pure unit tests
            # do not create a device, so skip the unrelated DXVK renderer-log gate.
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_D3D11_SKIP_DXVK_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-direct2d.sh")
        endif()
    endif()

    include(GoogleTest)
    # REMED-BUILD-001: without WORKING_DIRECTORY, CMake defaults every discovered case's cwd to
    # CnaTests' own runtime output directory, not the repo root where tests/assets/** lives --
    # CTest runs then fail to find fixture files that the same binary finds fine when run
    # directly from the repo root. Mirrors the already-correct pattern in
    # cmake/Tests/EasyGLTests.cmake / VulkanTests.cmake.
    gtest_discover_tests(CnaTests DISCOVERY_MODE PRE_TEST WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

    # D2D-118/D2D-119: a stable label and one explicit runner make the renderer's device-free
    # capability, blend, mip-policy, HRESULT and pixel-conversion contract independently runnable.
    if(CNA_GRAPHICS_RENDERER STREQUAL "DIRECT2D")
        cna_register_renderer_test(NAME Direct2D_Unit
            COMMAND CnaTests --gtest_filter=Direct2D*
            TIMEOUT 60 LABELS "Direct2D;Unit" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endif()

    # INPUT-BUILD-003: canonical Input-test selector — SINGLE SOURCE OF TRUTH.
    # The input subset used to be selected by a long --gtest_filter string copy-pasted across the docs
    # and CI, which silently drifted whenever a new suite name fell outside its tokens (e.g. the
    # ButtonState/KeyState/Buttons suites from INPUT-TEST-001). Keep the token list HERE only; docs and
    # CI select the subset via `ctest -L input` (see docs/input-build-and-test.md). When you add an input
    # test suite whose name matches none of these tokens, extend this one string.
    set(CNA_INPUT_TEST_FILTER
        "*Keyboard*:*Mouse*:*GamePad*:*Touch*:*Gesture*:*TextInput*:*SdlInputBridge*:*PlatformInputBridge*:*InputResetAllForTests*:*FakeGamepad*:*SdlGamepadSubsystemInit*:*ButtonState*:*KeyState*:*Buttons*:*PublicApiInput*:*CnaInput*:*Joystick*:*Haptic*")
    # INPUT-BUILD-009: `--gtest_shuffle --gtest_repeat=5` is the standardized order-independence gate.
    # The input state is a process-wide singleton (InputManager / GestureDetector / MouseCursor stock
    # cursors persist for the whole binary), so a static-state leak would reintroduce order dependence;
    # running the filtered subset 5x under a fresh shuffle each iteration is the required check that
    # catches it. `ctest -L input` runs exactly this on every renderer/CI job.
    # Headless-safe audio everywhere; the video driver is left to the runner (Xvfb+x11 in CI, real
    # display or `xvfb-run` locally) because the MouseCursor tests need real cursors (the SDL dummy
    # driver has null cursors).
    cna_register_renderer_test(NAME CnaInputTests COMMAND CnaTests --gtest_filter=${CNA_INPUT_TEST_FILTER} --gtest_shuffle --gtest_repeat=5
        LABELS "input" ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    # plans/plan_gltf.md GLTF-010: the glTF conformance ladder as one runnable label.
    #
    # `ctest -L gltf-conformance` runs the whole ladder. Each rung is registered as its OWN ctest
    # entry rather than one filter over all of them, because the ladder's whole point is that a
    # divergence has a *layer*: CTest's own per-test result then names it -- "GltfConformanceL4
    # (Failed)" -- without anyone reading a log. The rungs are listed lowest-first, and CTest runs
    # them in registration order, so the first failing entry is the first divergent layer
    # (GLTF-402). Higher layers still run, because knowing whether a wrong world matrix also
    # corrupted the bound effect parameters is worth a few extra milliseconds.
    #
    # L0 is not a ladder rung: it is the corpus itself -- generator determinism, manifest/asset
    # agreement, .glb/.gltf equivalence -- and if it fails, no layer above it means anything.
    # L7 (rendered image, GLTF-009) is deliberately absent: it needs a renderer with a real 3D
    # pipeline, so it will register alongside the others only on a renderer that has one.
    # SINGLE SOURCE OF TRUTH for the ladder's partition. `GltfConformanceLadder` (a gtest case)
    # parses this exact list out of this file and asserts that every registered Gltf* suite falls
    # into exactly one rung -- so a new suite that matches no rung fails the run instead of
    # silently sitting outside `ctest -L gltf-conformance`. Keep the entries here only.
    # plans/plan_modern.md MOD-1309/MOD-1310 add two suites that exist only when the engine layer is
    # compiled in. They are named here unconditionally -- the rung list is parsed as TEXT by
    # GltfConformanceLadderTests, so a CMake variable inside one of these strings would be read
    # literally -- and that test knows which of them are CNAEXT-gated.
    set(CNA_GLTF_CONFORMANCE_RUNGS
        "L0|GltfFixtureCorpus.*:GltfOracleEXT.*:GltfConformanceLadder.*:GltfSharedDefectPolicy.*:GltfRendererPbrFallbackPolicy.*:GltfRendererIndexWidthPolicy.*:GltfRendererPointTopologyPolicy.*:GltfDracoEncoderPin.*"
        "L1|GltfConformanceL1.*:GltfContainerRobustness.*:GltfContainerValidation.*:GltfUriContainment.*:GltfExternalBuffer.*:GltfExtensionRegistry.*:GltfLimitationsDoc.*:GltfVendoredCgltf.*"
        "L2|GltfConformanceL2.*:GltfAccessorDecodeLock.*:GltfBufferAndWeightForm.*:GltfIndexDecode.*:GltfIndexForm.*"
        "L3|GltfConformanceL3.*:GltfAttributeCoverage.*:GltfImportCoreTest.*:GltfPrimitiveTopology.*:GltfMaterialState.*:GltfMaterialVariants.*:GltfMaterialBridgeTest.*:GltfMaterialExtensionsTest.*:GltfMaterialToPbrMaterialTest.*:GltfDrawTopology.*:GltfSamplerMapping.*:GltfImageSource.*:GltfUvChannel.*:GltfOcclusionRemap.*:GltfUnsupportedTexture.*:GltfUnlitMaterial.*:GltfDracoParity.*"
        "L4|GltfConformanceL4.*:GltfConventions.*:GltfImportReport.*:GltfNodeTransformOrder.*:GltfNodeHierarchy.*:GltfMirroring.*:GltfModelShape.*:GltfSceneGraphBones.*:GltfSkinSpaces.*:GltfSkinComposition.*:GltfSkinLadder.*:GltfRigidAnimation.*:GltfAnimationSampling.*:GltfAnimationRobustness.*:GltfClipAndLight.*:GltfCameras.*:GltfMorphWeights.*:GltfMorphBlending.*:GltfSceneSelection.*:GltfRealWorldAcceptanceL4.*"
        "L5|GltfConformanceL5.*:GltfStrideAndBuffer.*:GltfBufferOracle.*:GltfVertexBufferInvariants.*:GltfVertexLayoutTable.*:GltfNormalTangentCorpus.*"
        "L6|GltfConformanceL6.*:GltfDrawParamsOracleL6.*:GltfLightingPolicy.*:GltfLightBudget.*:GltfPbrBrdf.*"
        "Perf|GltfPerformance.*"
        "Ledger|GltfKnownDefect.*"
        "Tool|GltfToCnjToolTest.*:RuntimeGltfModelTest.*:CnbGltfDirectToolTest.*")
    foreach(_gltf_rung IN LISTS CNA_GLTF_CONFORMANCE_RUNGS)
        string(FIND "${_gltf_rung}" "|" _gltf_sep)
        string(SUBSTRING "${_gltf_rung}" 0 ${_gltf_sep} _gltf_layer)
        math(EXPR _gltf_filter_start "${_gltf_sep} + 1")
        string(SUBSTRING "${_gltf_rung}" ${_gltf_filter_start} -1 _gltf_filter)
        cna_register_renderer_test(NAME CnaGltfConformance${_gltf_layer}
            COMMAND CnaTests --gtest_filter=${_gltf_filter}
            TIMEOUT 300 LABELS "gltf-conformance" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()

    # plans/plan_platform.md PLAT-91: audio has its own platform contract and selection axis rather
    # than inheriting from CNA::Platform. Keep its native-SDK-free/lifecycle/buffer-granularity
    # contract visible as a dedicated CTest; the shared conformance suite runs every implementation
    # compiled into the selected build (SDL3 + NULL by default, NULL in the SDL-free build).
    cna_register_renderer_test(NAME CnaAudioPlatformTests
        COMMAND CnaTests --gtest_filter=Audio*DeviceContractTests.*:*AudioDeviceConformanceTests.*:NullAudioDeviceTests.*:AudioPlatformSelectionCompileTests.*:Sdl2AudioDeviceTests.*:Sdl3AudioDeviceTests.*:Sdl3AudioRecordingDeviceTests.*:AudioMixerPlatformContractTests.* --gtest_shuffle --gtest_repeat=3
        LABELS "audio;platform" ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    # plans/plan_platform.md PLAT-93: test the cache default, every implemented value, every reserved
    # future identifier, and an unknown value without spawning six full nested project configs.
    # The selection file is intentionally script-mode-safe for exactly this validation path.
    foreach(_cna_audio_selection_case IN ITEMS DEFAULT SDL3 SDL2 NULL OPENAL WASAPI ALSA BOGUS)
        if(_cna_audio_selection_case STREQUAL "DEFAULT" OR
           _cna_audio_selection_case STREQUAL "SDL3")
            set(_cna_audio_selection_expected "Using SDL3 audio platform implementation")
        elseif(_cna_audio_selection_case STREQUAL "SDL2")
            set(_cna_audio_selection_expected "Using SDL2 audio platform implementation")
        elseif(_cna_audio_selection_case STREQUAL "NULL")
            set(_cna_audio_selection_expected "Using NULL audio platform implementation")
        elseif(_cna_audio_selection_case STREQUAL "BOGUS")
            set(_cna_audio_selection_expected "not a known audio platform")
        else()
            # CMake line-wraps the full diagnostic between "NOT" and "implemented" depending
            # on terminal width; this stable prefix still proves the reserved-value branch ran.
            set(_cna_audio_selection_expected "is a reserved identifier")
        endif()
        add_test(NAME CnaAudioPlatformSelection_${_cna_audio_selection_case}
            COMMAND ${CMAKE_COMMAND}
                -DCNA_AUDIO_SELECTION_FILE=${CMAKE_SOURCE_DIR}/cmake/AudioPlatformSelection.cmake
                -DCNA_AUDIO_SELECTION_CASE=${_cna_audio_selection_case}
                -DCNA_AUDIO_SELECTION_EXPECTED=${_cna_audio_selection_expected}
                -P ${CMAKE_SOURCE_DIR}/cmake/Tests/AudioPlatformSelectionCase.cmake)
        set_tests_properties(CnaAudioPlatformSelection_${_cna_audio_selection_case}
            PROPERTIES LABELS "audio;platform")
    endforeach()

    # plans/plan_xnapipeline.md XNAP-026: an independent, non-CNA implementation of the .xnb
    # reader side, run over the fixture corpus. CNA's own reader and writer were built together
    # and can share a mistake; this checker was written from the published format specification
    # and validates every real, externally produced uncompressed fixture in the repository before
    # it is trusted to judge CNA's own output.
    if(NOT DEFINED Python3_Interpreter_FOUND)
        find_package(Python3 QUIET COMPONENTS Interpreter)
    endif()
    if(Python3_Interpreter_FOUND)
        file(GLOB _cna_xnb_conformance_files
            "${CMAKE_SOURCE_DIR}/tests/assets/xnb/cna/windows/uncompressed/*.xnb"
            "${CMAKE_SOURCE_DIR}/tests/assets/xnb/cna/windows/lzx/*.xnb"
            "${CMAKE_SOURCE_DIR}/tests/assets/xnb/monogame/windows/lzx/*.xnb"
            "${CMAKE_SOURCE_DIR}/tests/assets/xnb/monogame/windows/uncompressed/*.xnb"
            "${CMAKE_SOURCE_DIR}/tests/assets/xnb/monogame/windows/uncompressed/audio/*.xnb"
            "${CMAKE_SOURCE_DIR}/tests/assets/xnb/monogame/windows/uncompressed/song/*.xnb"
            "${CMAKE_SOURCE_DIR}/tests/assets/xnb/xna40/windows/uncompressed/*.xnb")
        if(_cna_xnb_conformance_files)
            add_test(NAME CnaXnbSpecificationConformance
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tools/xnb/xnb_conformance.py"
                    ${_cna_xnb_conformance_files})
            set_tests_properties(CnaXnbSpecificationConformance
                PROPERTIES LABELS "content;xnb;conformance")
        endif()
        unset(_cna_xnb_conformance_files)

        # plans/plan_xnapipeline.md XNAP-59: every committed glTF fixture is built to Model XNB
        # and checked against its recorded outcome, with the independent specification parser
        # accepting every generated file. The sweep existed as a sentence somebody wrote after
        # running it once; a sweep nobody can re-run cannot notice a fixture that stopped
        # building, a refusal that started saying something else, or a fixture added later.
        if(TARGET cna_content_tool)
            add_test(NAME CnaXnbModelCorpusSweep
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tools/xnb/model_corpus_sweep.py"
                    --content-tool "$<TARGET_FILE:cna_content_tool>"
                    --python "${Python3_EXECUTABLE}")
            set_tests_properties(CnaXnbModelCorpusSweep
                PROPERTIES LABELS "content;xnb;conformance;model" TIMEOUT 900)
        endif()

        # plans/plan_xnapipeline.md XNAP-9B: the plan's stated task totals must agree with its own
        # task table. They disagreed once, in a way no reader could be expected to catch.
        add_test(NAME CnaXnbPlanStatusConsistency
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tools/xnb/check_plan_status.py"
                "${CMAKE_SOURCE_DIR}/plans/plan_xnapipeline.md")
        set_tests_properties(CnaXnbPlanStatusConsistency
            PROPERTIES LABELS "content;xnb;documentation")
    endif()

    add_test(NAME CnaSdl2OnlyRendererGate
        COMMAND ${CMAKE_COMMAND}
            -DCNA_SDL2_ONLY_GUARD_FILE=${CMAKE_SOURCE_DIR}/cmake/Sdl2OnlyConfiguration.cmake
            -P ${CMAKE_SOURCE_DIR}/cmake/Tests/Sdl2OnlyRendererGate.cmake)
    set_tests_properties(CnaSdl2OnlyRendererGate PROPERTIES LABELS "platform;configuration")

    # plans/plan_platform.md PLAT-30/31/32: the Sdl3Window tests need a live video subsystem, and they
    # get one from SDL's dummy driver rather than a display server. That only works in a process
    # where nothing has already committed SDL to a driver -- inside the shared CnaTests binary
    # another suite usually has, so these skip there and would otherwise contribute no coverage
    # at all. Running them as their own ctest, in their own process, is what makes them real.
    # The window half of the conformance suite (PLAT-116) and GraphicsDevice's platform-window
    # ownership regression (PLAT-62) belong here rather than with the rest
    # of it: its SDL3 parameterisation skips outright without a video subsystem, so running it in
    # the display-independent suite would have exercised only the implementations that need no
    # display. It ran nowhere at all until PLAT-130 -- the other suite's *PlatformConformance*
    # token does not match the string "PlatformWindowConformance".
    cna_register_renderer_test(NAME CnaPlatformWindowTests COMMAND CnaTests --gtest_filter=Sdl3WindowTest.*:Sdl3DisplayTest.*:Sdl3GraphicsServiceTest.*:Sdl3PresenterTest.*:Sdl3InputTest.TextInputLifecycleAndAreaReachALivePlatformWindow:DisplayInfoTests.*:GraphicsDevicePlatformWindowTests.*:GameWindowPlatformTest.*:*PlatformWindowConformance*
        LABELS "platform" ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

    # The rest of the platform contract is display-independent by construction, so it runs
    # unconditionally. Shuffled and repeated for the same reason the input suite is: SDL's
    # subsystem refcount is process-global, and an acquire/release imbalance would show up as
    # order dependence rather than as a direct failure.
    cna_register_renderer_test(NAME CnaPlatformTests
        COMMAND CnaTests --gtest_filter=NativeWindow*:Platform*:IPlatform*:ServiceContract*:InputSnapshot*:SystemService*:WindowDescription*:GlContext*:VulkanSurface*:ContractIsSdlFree*:Sdl3PlatformTest.*:Sdl3EventMapperTests.*:Sdl3InputTest.*:Sdl3ServiceTest.*:*PlatformConformance*:HeadlessPlatform*:TerminalPlatformTest.*:TerminalCapabilityProbeTests.*:TerminalKeyboardTest.*:TerminalMouseTest.*:TerminalSessionTest.*:TerminalRestoration.*:TerminalFrameGridTest.*:TerminalAnsiWriterTest.*:TerminalPresenter.*:TerminalResize*:TerminalFrameBudgetTest.*:TerminalBudgetPresenter.*:CurrentPlatformTest.*
                --gtest_shuffle --gtest_repeat=3
        LABELS "platform" ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    if(MINGW)
        # Statically link MinGW runtime into the test binary too, so it can
        # run outside CLion without libgcc_s_seh-1.dll / libstdc++-6.dll.
        target_link_options(CnaTests PRIVATE -static-libgcc -static-libstdc++)
        # libwinpthread-1.dll cannot be statically linked; copy it next to the exe.
        cna_copy_mingw_runtime(CnaTests)
    endif()

    if(WIN32)
        cna_copy_sdl_runtime(CnaTests)
        foreach(_gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
            if(TARGET ${_gtest_target})
                add_custom_command(TARGET CnaTests POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:${_gtest_target}>
                        $<TARGET_FILE_DIR:CnaTests>
                    VERBATIM)
            endif()
        endforeach()
    endif()
endif()

# Task 470: headless-safe CTest skip convention. A test that self-detects "no GPU/display
# available" (e.g. SDL_InitSubSystem(SDL_INIT_VIDEO) failing, see
# modules/graphics/examples/common/PixelTestGame.hpp's RunPixelTest()) and exits with this sentinel code is
# reported by ctest as SKIPPED rather than FAILED. Applied here in one shot to every test
# already registered anywhere in this file, rather than adding a SKIP_RETURN_CODE property to
# each individual set_tests_properties() call across ~330 existing test registrations -- purely
# additive: a test that never exits with this code is completely unaffected. 77 is the
# conventional Automake/BSD "test skipped" exit code, also CMake's own documented example value
# for SKIP_RETURN_CODE.
get_property(_cna_all_tests DIRECTORY PROPERTY TESTS)
if(_cna_all_tests)
    set_tests_properties(${_cna_all_tests} PROPERTIES SKIP_RETURN_CODE 77)
endif()
