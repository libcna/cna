# plan_runtimerenderer.md RTR-P9-19: renderer gates that decide whether a renderer's OWN tests are
# compiled, or its libraries exposed to the test executable, test LIST MEMBERSHIP
# (CNA_RENDERER_IDENTITIES) rather than equality with the build default. In single-renderer mode the
# list holds one entry and the two are identical; in a multi-renderer build a renderer's tests must
# be kept whenever that renderer is compiled in, not only when it happens to be the default --
# otherwise a multi build silently drops the suites of every non-default renderer it contains.
#
# The CROSSCOMPILING_EMULATOR chain further down is deliberately NOT converted: a target carries one
# emulator property, so it can only ever describe one renderer, and the default is the honest choice.

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

    # plan_binding.md CBIND-033: the C API module owns its own test executables
    # (modules/c-api/CMakeLists.txt) and none of its C++ translation units is a GTest unit --
    # HandleRegistryTest.cpp and BoundaryDetailTest.cpp define their own main(), and
    # AbiHeaderCpp.cpp is a pure ABI compile check linked into cna_c_api_abi_smoke. Letting the
    # recursive glob add them to CnaTests fails the build at <CNA/C/abi.h> (the C API's include
    # path is scoped to its own targets) and would duplicate main -- same reason as the Glide ABI
    # programs and the module probes above.
    list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/modules/c-api/tests/.*\\.cpp$")

    # plan_apple.md APPLE-11: the Apple smoke application (cmake/AppleSmoke.cmake) is a complete
    # program with its own main(), for the same reason as the two entries above. Swept into
    # CnaTests it does not merely add a case -- its main() replaces GTest's, so the test binary
    # links, runs the smoke app and exits without running a single test.
    list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/tests/apple/.*\\.cpp$")

    # plan_platform.md PLAT-119: the platform module's SDL3-specific tests exercise
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

    # plan_platform.md PLAT-94: like the general platform implementation above, the native
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
        foreach(_cna_sdl3_mixer_test IN ITEMS
                AudioMixerTests
                CueTests
                DynamicSoundEffectInstanceTests
                OfflineAudioRendererTests
                SoundBankTests
                SoundEffectInstanceTests
                SoundEffectTests)
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

    # plan_platform.md PLAT-130: TerminalPlatform is built on termios and pseudo-terminals, so
    # both it and its tests are POSIX-only -- excluded on Windows for the same reason the
    # implementation directory is (modules/platform/CMakeLists.txt), not gated line-by-line.
    # Everywhere else they always build, whatever CNA_PLATFORM says, because the implementation
    # always does.
    if(WIN32)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/modules/platform/tests/.*/Terminal.*\\.cpp$")
    endif()

    # REMED-BUILD-013 (discovered while verifying it): mirrors CnaLibrary.cmake's own
    # CNA_FFMPEG_AVAILABLE exclusion for VideoDecoder.cpp/VideoPlayer.cpp/Video.cpp/
    # VideoContentTypeReader.cpp -- these 4 test files exercise exactly those classes, which do not
    # exist in the built CNA library on an FFmpeg-unavailable platform (MinGW/Emscripten/Android/iOS),
    # and previously failed to link there (never hit before this task: no FFmpeg-unavailable CnaTests
    # build had gotten this far). VideoSoundtrackTypeTests.cpp is unaffected -- that enum has no .cpp
    # and no FFmpeg dependency.
    if(NOT CNA_FFMPEG_AVAILABLE)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Media/VideoDecoderTests\\.cpp$")
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Xnb/VideoContentTypeReaderTests\\.cpp$")
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
    # with its own bundled assets. iOS (plan_apple.md APPLE-5) is excluded for both of those
    # reasons at once: an app-sandboxed process may not spawn another executable at all, and the
    # baked-in build-machine harness path does not exist inside the .app either. Not part of the Task 6.2/6.4 verification filters
    # (*Network*:*Gamer*:*ENet*:*Packet*) either, since its suite name is TwoProcessLoopbackTest.
    # plan_dx1.md DX1-88 regression pass: found and fixed a pre-existing, not-DX1-specific gap --
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

    # GltfToCnjToolTests.cpp (plan_cnj.md CNB-52) uses the same POSIX-only posix_spawn/sys-wait
    # process APIs to spawn cna_tool_gltf_to_cnj as an independent OS process, for the same
    # reasons as the harness-spawning tests above.
    if(WIN32 OR EMSCRIPTEN OR ANDROID OR CNA_APPLE_IOS)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/Microsoft/Xna/Framework/Content/GltfToCnjToolTests\\.cpp$")
    endif()

    # DevicesShutdownOrderingTests.cpp (Task SDLCORE-011) uses the same POSIX-only process APIs
    # (posix_spawn, poll, sys/wait.h) to spawn tools/devices/shutdown_ordering_harness.cpp as an
    # independent OS process, for the same reason TwoProcessLoopbackTest.cpp needs one above.
    # Excluded on the same platforms and for the same reasons.
    if(WIN32 OR EMSCRIPTEN OR ANDROID OR CNA_APPLE_IOS)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/Microsoft/Devices/Detail/DevicesShutdownOrderingTests\\.cpp$")
    endif()

    # plan_wicked.md: the Wicked renderer's own regression suites live under
    # modules/renderers/wicked/tests/, and the pipeline-key test among them includes the
    # renderer's header, which resolves only when the WICKED renderer is configured (the
    # WickedEngine include directories come with the renderer target). Excluded from every other
    # renderer's corpus the same way the conditional files above are -- an unconditional glob of
    # a renderer-local test directory otherwise breaks every other renderer's CnaTests configure.
    # Under WICKED the corpus keeps them, and the dedicated cna_test_wicked_* targets
    # (cmake/Tests/WickedTests.cmake) build them standalone as well.
    if(NOT "WICKED" IN_LIST CNA_RENDERER_IDENTITIES)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Renderers/Wicked/.*\\.cpp$")
    endif()

    # plan_magnum.md: the MAGNUM renderer's own GTest suite lives under
    # modules/renderers/magnum/tests/ and includes the renderer's headers, which resolve only
    # when the MAGNUM renderer is configured (the Magnum::GL include directories come with the
    # renderer target). Excluded from every other renderer's corpus by the same convention as the
    # Wicked directory above; under MAGNUM the corpus keeps it.
    if(NOT "MAGNUM" IN_LIST CNA_RENDERER_IDENTITIES)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Renderers/Magnum/.*\\.cpp$")
    endif()

    # plan_fna3d.md: the FNA3D renderer's own GTest suites live under
    # modules/renderers/fna3d/tests/ and include the renderer's headers, which resolve only when
    # the FNA3D renderer is configured (the FNA3D/MojoShader include roots come with the renderer
    # target). Excluded from every other renderer's corpus by the same convention as the Wicked
    # and Magnum directories above; under FNA3D the corpus keeps them.
    if(NOT "FNA3D" IN_LIST CNA_RENDERER_IDENTITIES)
        list(FILTER CNA_TEST_SOURCES EXCLUDE REGEX ".*/CNA/Internal/Renderers/Fna3d/.*\\.cpp$")
    endif()

    add_executable(CnaTests
            ${CNA_TEST_SOURCES}
    )

    # mingw-w64's <cmath> only exposes M_PI when _USE_MATH_DEFINES is set (unlike glibc, which
    # defines it unconditionally) — a handful of test files use M_PI directly as a reference value.
    if(MINGW)
        target_compile_definitions(CnaTests PRIVATE _USE_MATH_DEFINES)
        # Known mingw-w64/GCC PE-COFF toolchain limitation: std::type_info::operator== is emitted
        # as vague linkage (COMDAT) in libstdc++.a, but a test binary this large (many RTTI-using
        # TUs) trips a case where the linker fails to fold it against the identical libstdc++
        # definition. Both copies are byte-identical, so allowing the duplicate is safe.
        target_link_options(CnaTests PRIVATE -Wl,--allow-multiple-definition)
    endif()

    if(EMSCRIPTEN)
        # Emscripten's EXIT_RUNTIME defaults to 0 (deliberately keeps the JS runtime alive after
        # main() returns, so pending async JS/WebSocket handles can keep running) - without this,
        # `node CnaTests.js` never exits after printing its results, even on success.
        target_link_options(CnaTests PRIVATE -sEXIT_RUNTIME=1)

        # Emscripten's default build is fully synchronous/single-threaded: a real WebSocket
        # handshake structurally cannot complete while C++ code holds the call stack (confirmed
        # empirically - even a real 1s sleep loop never lets it finish, since nothing returns
        # control to Node's event loop). The SystemLink loopback tests need emscripten_sleep() to
        # actually yield back to Node between polls, which requires Asyncify. Scoped to CnaTests
        # only (a per-executable link-time transformation - does not affect native/Windows builds
        # or any other Emscripten executable target such as the demos or the two-process harness).
        target_link_options(CnaTests PRIVATE -sASYNCIFY=1)
    endif()

    target_link_libraries(CnaTests
            PRIVATE
            CNA
            SHARP_RUNTIME
            gtest_main
            SDL3::SDL3
    )

    # The Draco corpus owns one test-only encoder oracle: it recreates the committed compressed
    # streams with CNA's pinned dependency and byte-compares them, so the otherwise standard-
    # library-only Python generator never needs to execute a native tool. Production content code
    # keeps Draco PRIVATE; expose it only to CnaTests when that optional decoder is configured.
    if(CNA_DRACO_AVAILABLE)
        target_link_libraries(CnaTests PRIVATE cna_draco)
        if(NOT CNA_USE_SYSTEM_DRACO)
            target_compile_definitions(CnaTests PRIVATE CNA_VENDORED_DRACO)
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
    target_link_libraries(CnaTests PRIVATE cna_renderer_metal_headers cna_renderer_glide_headers)

    # GltfImportCoreTests.cpp includes CNA/Internal/GltfImport/GltfImportCore.hpp directly (to call
    # ExtractMesh() without spawning the CLI tool), which itself includes cgltf.h -- CNA's own
    # target_include_directories for that path is PRIVATE (see cmake/CnaLibrary.cmake), so it does
    # not propagate to CnaTests via target_link_libraries and must be added here too.
    target_include_directories(CnaTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
            # plan_runtimerenderer.md RTR-P9-2: shared test-support headers reached by their
            # namespace path (CNA/RendererTestGate.hpp), matching how every module's public headers
            # are spelled.
            ${CMAKE_CURRENT_SOURCE_DIR}/modules/graphics/tests
            # plan_platform.md PLAT-77/PLAT-90: modules/platform/tests holds shared test
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
    )

    # plan_fx.md FX-060: the shared compiled-effect conformance suite is header-only test support
    # (fixtures plus the contract assertions every CompiledEffects backend must satisfy). It lives
    # under the top-level tests/ tree because it belongs to no single renderer module -- the point
    # is that a new backend runs the identical contract with only its own device setup.
    target_include_directories(CnaTests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
    )

    # plan_runtimerenderer.md RTR-P9-9: one CNA_RENDERER_PRESENT_<IDENTITY> define per renderer
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
        target_compile_definitions(CnaTests PRIVATE "CNA_RENDERER_PRESENT_${_cna_present_identity}")
    endforeach()

    # ...and each compiled-in renderer's own public include root, for the same reason: those suites
    # include their renderer's headers ("CNA/Internal/Renderers/Magnum/MagnumBuffers.hpp"), and only
    # the DEFAULT renderer's target contributes its include directory to this executable. Enabling
    # the define without the include root just moves the failure from "no tests" to "no such file".
    foreach(_cna_present_dir IN LISTS CNA_RENDERER_DIRS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_cna_present_dir}/include")
            target_include_directories(CnaTests PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/${_cna_present_dir}/include")
        endif()
    endforeach()

    # plan_runtimerenderer.md RTR-P9-9, second half: a module's own include root is not enough when
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
            target_include_directories(CnaTests PRIVATE
                "$<TARGET_PROPERTY:${_cna_present_target},INCLUDE_DIRECTORIES>")
        endif()
    endforeach()

    # REMED-GFX-054's WebGPU-only IndexBuffer regression opens native error scopes around the
    # public operation. CNA's renderer intentionally keeps wgpu-native PRIVATE, so expose it only
    # to this test executable in the WebGPU configuration.
    # plan_runtimerenderer.md RTR-P10-12: FNA3D's suites include mojoshader.h, and that header
    # includes the GENERATED mojoshader_version.h unless MOJOSHADER_NO_VERSION_INCLUDE is defined.
    # cna_fna3d carries that switch for the renderer target, but RTR-P9-9 gave this executable the
    # present renderers' include PATHS only -- so CnaTests found the header and then failed on the
    # generated one it pulls in. The include root alone is not the whole surface a family's suites
    # compile against; its switches are part of it.
    if("FNA3D" IN_LIST CNA_RENDERER_IDENTITIES AND TARGET cna_fna3d)
        target_link_libraries(CnaTests PRIVATE cna_fna3d)
    endif()

    if("WEBGPU" IN_LIST CNA_RENDERER_IDENTITIES)
        target_link_libraries(CnaTests PRIVATE WebGPU::WebGPU)
    endif()

    # plan_magnum.md MAGNUM-40: the MAGNUM renderer's own tests exercise its XNA-ordinal -> Magnum-enum
    # mappings and its generated stock GLSL directly, so they include Magnum's GL headers. CNA keeps
    # Magnum PRIVATE on the renderer target (same discipline as wgpu-native above), so it is exposed
    # to this test executable only, and only in the Magnum configuration.
    if("MAGNUM" IN_LIST CNA_RENDERER_IDENTITIES)
        target_link_libraries(CnaTests PRIVATE Magnum::GL Magnum::Magnum)
    endif()

    # plan_diligent.md DILIGENT-15: DiligentDeviceSelectionTests.cpp includes the renderer header,
    # which includes DiligentCore's own headers. cna_link_diligent() keeps those PRIVATE to the
    # renderer target (same discipline as WebGPU just above), so expose them here too.
    if("DILIGENT" IN_LIST CNA_RENDERER_IDENTITIES)
        cna_link_diligent(CnaTests)
    endif()

    if(CNA_ENABLE_NET)
        target_link_libraries(CnaTests PRIVATE CNA_GamerServices CNA_Net)
    endif()

    if(TARGET cna_net_two_process_harness)
        # Building CnaTests must also build the harness it spawns at test time (they're separate
        # executables, so CMake won't infer this dependency on its own), and the orchestrator test
        # needs the harness's real built path baked in at compile time.
        add_dependencies(CnaTests cna_net_two_process_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_NET_HARNESS_PATH="$<TARGET_FILE:cna_net_two_process_harness>"
        )
    endif()

    if(TARGET cna_net_gamerservices_dispatcher_harness)
        # Same reasoning as cna_net_two_process_harness just above.
        add_dependencies(CnaTests cna_net_gamerservices_dispatcher_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_NET_GAMERSERVICES_HANG_HARNESS_PATH="$<TARGET_FILE:cna_net_gamerservices_dispatcher_harness>"
        )
    endif()

    if(TARGET cna_audio_no_hardware_harness)
        # Same reasoning as cna_net_two_process_harness above, for AudioMixerTests.cpp.
        add_dependencies(CnaTests cna_audio_no_hardware_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_AUDIO_NO_HARDWARE_HARNESS_PATH="$<TARGET_FILE:cna_audio_no_hardware_harness>"
        )
    endif()

    if(TARGET cna_tool_gltf_to_cnj)
        # plan_cnj.md CNB-52: GltfToCnjToolTests.cpp spawns the real converter tool as a
        # subprocess (same reasoning as cna_net_two_process_harness above -- a separate
        # executable with its own main(), not a library call) and needs its real built path
        # baked in at compile time.
        add_dependencies(CnaTests cna_tool_gltf_to_cnj)
        target_compile_definitions(CnaTests PRIVATE
            CNA_GLTF_TO_CNJ_TOOL_PATH="$<TARGET_FILE:cna_tool_gltf_to_cnj>"
        )
    endif()

    if(TARGET cna_devices_shutdown_ordering_harness)
        # Same reasoning as cna_net_two_process_harness above, for
        # DevicesShutdownOrderingTests.cpp (Task SDLCORE-011) -- calls the real SDL_Quit(),
        # which must not run inside the shared CnaTests process itself.
        add_dependencies(CnaTests cna_devices_shutdown_ordering_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_DEVICES_SHUTDOWN_ORDERING_HARNESS_PATH="$<TARGET_FILE:cna_devices_shutdown_ordering_harness>"
        )
    endif()

    if(TARGET cna_platform_terminal_restoration_harness)
        # plan_platform.md PLAT-131: same reasoning as cna_net_two_process_harness above, and more
        # sharply -- four of the five exit paths TerminalSession must restore on destroy the
        # process, so they cannot be asserted inside this binary at all.
        add_dependencies(CnaTests cna_platform_terminal_restoration_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_PLATFORM_TERMINAL_RESTORATION_HARNESS_PATH="$<TARGET_FILE:cna_platform_terminal_restoration_harness>"
        )
    endif()

    if(TARGET cna_platform_terminal_resize_harness)
        # plan_platform.md PLAT-136: see cna_platform_terminal_restoration_harness above.
        add_dependencies(CnaTests cna_platform_terminal_resize_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_PLATFORM_TERMINAL_RESIZE_HARNESS_PATH="$<TARGET_FILE:cna_platform_terminal_resize_harness>"
        )
    endif()

    if(TARGET cna_audio_mixer_destroy_active_static_voice_harness)
        # Task AUD-04-008: same reasoning as cna_net_two_process_harness above.
        add_dependencies(CnaTests cna_audio_mixer_destroy_active_static_voice_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_AUDIO_MIXER_DESTROY_ACTIVE_STATIC_VOICE_HARNESS_PATH="$<TARGET_FILE:cna_audio_mixer_destroy_active_static_voice_harness>"
        )
    endif()

    if(TARGET cna_audio_mixer_destroy_active_dynamic_voice_harness)
        # Task AUD-04-009: same reasoning as cna_net_two_process_harness above.
        add_dependencies(CnaTests cna_audio_mixer_destroy_active_dynamic_voice_harness)
        target_compile_definitions(CnaTests PRIVATE
            CNA_AUDIO_MIXER_DESTROY_ACTIVE_DYNAMIC_VOICE_HARNESS_PATH="$<TARGET_FILE:cna_audio_mixer_destroy_active_dynamic_voice_harness>"
        )
    endif()

    if(TARGET easy-gl)
        target_link_libraries(CnaTests PRIVATE easy-gl)
    endif()

    # plan_dx.md DX-15 follow-up + plan_dx9.md D9-123 follow-up (merge-reconciled 2026-07-16):
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
            # plan_dx2.md DX2-84 follow-up: this gap pre-dates DIRECTX2 (DIRECTX1 never got this wiring
            # either, plan_dx1.md's own DX1-88 full-suite regression must have run CnaTests.exe
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
            # plan_dx3.md: wired proactively (not discovered by a from-scratch regression this
            # time) -- same DX2-84 finding/fix, applied up front for this new renderer.
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX3_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx3.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX5")
            # plan_dx5.md: same proactive wiring as DIRECTX3.
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX5_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx5.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX6")
            # plan_dx6.md: same proactive wiring as DIRECTX3/DIRECTX5.
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX6_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx6.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX7")
            # plan_dx7.md: same proactive wiring as DIRECTX3/DIRECTX5/DIRECTX6.
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX7_SKIP_DDRAW_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx7.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX8")
            # plan_dx8.md: DXVK-delivered (not DirectDraw-based), same proactive wiring shape as
            # D3D9's own run-wine-dxvk9.sh gate -- CNA_DX8_SKIP_DXVK_GATE for a binary that
            # legitimately never opens a real D3D8 device (e.g. a bare --gtest_list_tests call).
            set_target_properties(CnaTests PROPERTIES
                CROSSCOMPILING_EMULATOR "${CMAKE_COMMAND};-E;env;CNA_DX8_SKIP_DXVK_GATE=1;bash;${CMAKE_SOURCE_DIR}/scripts/run-wine-directx8.sh")
        elseif(CNA_GRAPHICS_RENDERER STREQUAL "DIRECTX10")
            # plan_d3d10.md: DXVK-delivered (via d3d10core), same proactive wiring shape as DIRECTX8's
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

    # plan_gltf.md GLTF-010: the glTF conformance ladder as one runnable label.
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
    set(CNA_GLTF_CONFORMANCE_RUNGS
        "L0|GltfFixtureCorpus.*:GltfOracleEXT.*:GltfConformanceLadder.*:GltfSharedDefectPolicy.*:GltfRendererPbrFallbackPolicy.*:GltfRendererIndexWidthPolicy.*:GltfRendererPointTopologyPolicy.*:GltfDracoEncoderPin.*"
        "L1|GltfConformanceL1.*:GltfContainerRobustness.*:GltfContainerValidation.*:GltfUriContainment.*:GltfExternalBuffer.*:GltfExtensionRegistry.*:GltfLimitationsDoc.*:GltfVendoredCgltf.*"
        "L2|GltfConformanceL2.*:GltfAccessorDecodeLock.*:GltfBufferAndWeightForm.*:GltfIndexDecode.*:GltfIndexForm.*"
        "L3|GltfConformanceL3.*:GltfAttributeCoverage.*:GltfImportCoreTest.*:GltfPrimitiveTopology.*:GltfMaterialState.*:GltfMaterialVariants.*:GltfDrawTopology.*:GltfSamplerMapping.*:GltfImageSource.*:GltfUvChannel.*:GltfOcclusionRemap.*:GltfUnsupportedTexture.*:GltfUnlitMaterial.*:GltfDracoParity.*"
        "L4|GltfConformanceL4.*:GltfConventions.*:GltfImportReport.*:GltfNodeTransformOrder.*:GltfNodeHierarchy.*:GltfMirroring.*:GltfModelShape.*:GltfSceneGraphBones.*:GltfSkinSpaces.*:GltfSkinComposition.*:GltfSkinLadder.*:GltfRigidAnimation.*:GltfAnimationSampling.*:GltfAnimationRobustness.*:GltfClipAndLight.*:GltfCameras.*:GltfMorphWeights.*:GltfMorphBlending.*:GltfSceneSelection.*:GltfRealWorldAcceptanceL4.*"
        "L5|GltfConformanceL5.*:GltfStrideAndBuffer.*:GltfBufferOracle.*:GltfVertexBufferInvariants.*:GltfVertexLayoutTable.*:GltfNormalTangentCorpus.*"
        "L6|GltfConformanceL6.*:GltfDrawParamsOracleL6.*:GltfLightingPolicy.*:GltfLightBudget.*:GltfPbrBrdf.*"
        "Perf|GltfPerformance.*"
        "Ledger|GltfKnownDefect.*"
        "Tool|GltfToCnjToolTest.*:RuntimeGltfModelTest.*")
    foreach(_gltf_rung IN LISTS CNA_GLTF_CONFORMANCE_RUNGS)
        string(FIND "${_gltf_rung}" "|" _gltf_sep)
        string(SUBSTRING "${_gltf_rung}" 0 ${_gltf_sep} _gltf_layer)
        math(EXPR _gltf_filter_start "${_gltf_sep} + 1")
        string(SUBSTRING "${_gltf_rung}" ${_gltf_filter_start} -1 _gltf_filter)
        cna_register_renderer_test(NAME CnaGltfConformance${_gltf_layer}
            COMMAND CnaTests --gtest_filter=${_gltf_filter}
            TIMEOUT 300 LABELS "gltf-conformance" WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endforeach()

    # plan_platform.md PLAT-91: audio has its own platform contract and selection axis rather
    # than inheriting from CNA::Platform. Keep its native-SDK-free/lifecycle/buffer-granularity
    # contract visible as a dedicated CTest; the shared conformance suite runs every implementation
    # compiled into the selected build (SDL3 + NULL by default, NULL in the SDL-free build).
    cna_register_renderer_test(NAME CnaAudioPlatformTests
        COMMAND CnaTests --gtest_filter=Audio*DeviceContractTests.*:*AudioDeviceConformanceTests.*:NullAudioDeviceTests.*:AudioPlatformSelectionCompileTests.*:Sdl2AudioDeviceTests.*:Sdl3AudioDeviceTests.*:Sdl3AudioRecordingDeviceTests.*:AudioMixerPlatformContractTests.* --gtest_shuffle --gtest_repeat=3
        LABELS "audio;platform" ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    # plan_platform.md PLAT-93: test the cache default, every implemented value, every reserved
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

    add_test(NAME CnaSdl2OnlyRendererGate
        COMMAND ${CMAKE_COMMAND}
            -DCNA_SDL2_ONLY_GUARD_FILE=${CMAKE_SOURCE_DIR}/cmake/Sdl2OnlyConfiguration.cmake
            -P ${CMAKE_SOURCE_DIR}/cmake/Tests/Sdl2OnlyRendererGate.cmake)
    set_tests_properties(CnaSdl2OnlyRendererGate PROPERTIES LABELS "platform;configuration")

    # plan_platform.md PLAT-30/31/32: the Sdl3Window tests need a live video subsystem, and they
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
