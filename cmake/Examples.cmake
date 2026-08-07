# --- Examples ---
# The 3D house demo exercises the EasyGL 3D pipeline. Other backends throw
# "3D not supported", so the target is only enabled with a GL-family public backend
# (OPENGLES/OPENGL33/WEBGL1/WEBGL2, all implemented by EasyGL -- see plan_glbackends.md).
option(CNA_BUILD_EXAMPLES "Build CNA example applications (house3d demo, demo_2d, ...)" ON)
if(CNA_BUILD_EXAMPLES)
    # ---- 2D sprite demo (works with all backends) ----------------------------
    add_executable(cna_demo_2d
        examples/demo_2d/src/Main.cpp
        examples/demo_2d/src/Game1.cpp
    )
    target_include_directories(cna_demo_2d PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_2d PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_2d PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_2d PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=134217728
            -sFORCE_FILESYSTEM=1
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/Content@/Content"
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_2d PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_2d PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_2d PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_2d POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/Content"
                "$<TARGET_FILE_DIR:cna_demo_2d>/Content"
            COMMENT "Copying Content next to cna_demo_2d executable")
        if(CNA_GRAPHICS_BACKEND STREQUAL "WEBGPU" AND CNA_WEBGPU_RUNTIME_LIBRARY)
            add_custom_command(TARGET cna_demo_2d POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CNA_WEBGPU_RUNTIME_LIBRARY}" "$<TARGET_FILE_DIR:cna_demo_2d>"
                COMMENT "Copying wgpu-native runtime next to cna_demo_2d")
            if(UNIX AND NOT APPLE)
                set_property(TARGET cna_demo_2d APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            endif()
        endif()
        if(WIN32)
            set_target_properties(cna_demo_2d PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_2d)
        endif()
    endif()

    # ---- Audio demo (works with all backends) --------------------------------
    add_executable(cna_demo_sound
        examples/demo_sound/src/Main.cpp
        examples/demo_sound/src/SoundDemo.cpp
    )
    target_include_directories(cna_demo_sound PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_sound/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_sound PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_sound PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_sound PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=134217728
            -sFORCE_FILESYSTEM=1
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_sound/Content@/Content"
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_sound PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_sound PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_sound PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_sound POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_sound/Content"
                "$<TARGET_FILE_DIR:cna_demo_sound>/Content"
            COMMENT "Copying Content next to cna_demo_sound executable")
        if(WIN32)
            set_target_properties(cna_demo_sound PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_sound)
        endif()
    endif()

    # ---- XACT demo (desktop only — XACT is Windows/Xbox-specific, no web content) -
    if(NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_demo_xact
        examples/demo_xact/src/Main.cpp
        examples/demo_xact/src/XactDemo.cpp
    )
    target_include_directories(cna_demo_xact PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_xact/src
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_demo_xact PRIVATE
            -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
            SHARP_RUNTIME)
    else()
        target_link_libraries(cna_demo_xact PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_demo_xact PRIVATE SDL3::SDL3main)
    endif()
    # REMED-BUILD-002: no POST_BUILD Content copy here, unlike the other demo targets above --
    # examples/demo_xact/ has no Content/ directory at all. XactDemo::LoadContent() generates its
    # own XGS/XWB/XSB files at runtime via XactFileGen::GenerateXactFiles() into "Content/Audio"
    # relative to the process's working directory, so there is nothing to copy from source.
    if(WIN32)
        set_target_properties(cna_demo_xact PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_demo_xact)
    endif()
    endif() # NOT EMSCRIPTEN AND NOT ANDROID

    # ---- Input & Touch demo (works with all backends) ------------------------
    add_executable(cna_demo_input
        examples/demo_input/src/Main.cpp
        examples/demo_input/src/InputDemo.cpp
    )
    target_include_directories(cna_demo_input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_input/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_input PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_input PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_input PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864
            -sFORCE_FILESYSTEM=1
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_input PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_input PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_input PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_input PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_input)
        endif()
    endif()

    # ---- Devices (Sensors/VibrateController) demo (works with all backends) --
    # Desktop/Emscripten only: NOT ANDROID (Task DEV-BUILD-004). A plain
    # add_executable() cannot be a valid Android app target at all --
    # <SDL3/SDL_main.h> #defines main to SDL_main on Android
    # (SDL_MAIN_NEEDED, so SDLActivity.java's dlsym("SDL_main") lookup on a
    # shared library finds a real symbol), which leaves no literal `main`
    # symbol for a plain ELF executable's C runtime startup
    # (crtbegin_dynamic.o) to link against -- confirmed via a real Android
    # cross-compile attempt: FAILS at the *link* step with "undefined
    # symbol: main", not at compile time. The real, working Android build of
    # this same demo is the entirely separate Gradle/CMake project under
    # examples/demo_devices/android/ (docs/devices-build.md Section 4.1),
    # which compiles Main.cpp/DevicesDemo.cpp into a genuine shared library
    # ("main") via its own app/jni/src/CMakeLists.txt -- not this target.
    if(NOT ANDROID)
    add_executable(cna_demo_devices
        examples/demo_devices/src/Main.cpp
        examples/demo_devices/src/DevicesDemo.cpp
    )
    target_include_directories(cna_demo_devices PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_devices/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_devices PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_devices PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_devices PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864
            -sFORCE_FILESYSTEM=1
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_devices PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_devices PRIVATE CNA SHARP_RUNTIME)
        endif()
        # examples/demo_devices/src/Main.cpp includes <SDL3/SDL_main.h>
        # directly (needed on Android so SDLActivity.java's dlsym("SDL_main")
        # lookup finds a real symbol) -- unlike every other demo target,
        # which never touches an SDL3 header directly and only goes through
        # CNA's own abstraction layer. CNA links SDL3::SDL3 PRIVATE (line
        # ~222 above), so that include path does not propagate transitively
        # to any executable that only links CNA. On desktop this went
        # unnoticed because the host compiler's own default system include
        # path (e.g. /usr/local/include on this container) happens to carry
        # a coincidentally-installed system SDL3 dev package -- cross-
        # compiling for Android has no such host-path fallback, and fails
        # with "SDL3/SDL_main.h file not found". Link SDL3::SDL3 directly so
        # this target gets its own real include path on every platform,
        # independent of host-system luck.
        target_link_libraries(cna_demo_devices PRIVATE SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_devices PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_devices PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_devices)
        endif()
    endif()
    endif() # NOT ANDROID
endif()

# ---- Combined input smoke sample: Keyboard+Mouse+GamePad+Touch+TextInput -----
if(CNA_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_input_smoke examples/input_smoke/InputSmoke.cpp)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_input_smoke PRIVATE
            -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group SHARP_RUNTIME)
    else()
        target_link_libraries(cna_input_smoke PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_input_smoke PRIVATE SDL3::SDL3main)
    endif()
    if(WIN32)
        set_target_properties(cna_input_smoke PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_input_smoke)
    endif()
endif()

# ---- NOXNA settings example (no window needed — pure API test) ---------------
if(CNA_BUILD_EXAMPLES AND CNA_NOXNA)
    add_executable(cna_example_noxna_settings examples/noxna_settings_example.cpp)
    target_link_libraries(cna_example_noxna_settings PRIVATE CNA)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_example_noxna_settings PRIVATE SDL3::SDL3main)
    endif()
    if(CNA_BUILD_TESTS)
        add_test(NAME NOXNA_Settings_Compile_Run
                 COMMAND cna_example_noxna_settings)
    endif()
endif()

# ---- DepthEffect manual verification demo (renders + screenshots each colour-depth mode) -----
if(CNA_BUILD_EXAMPLES AND CNA_NOXNA AND (CNA_GRAPHICS_BACKEND STREQUAL "OPENGLES" OR CNA_GRAPHICS_BACKEND STREQUAL "OPENGL33") AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_depth_effect_demo examples/depth_effect_demo_test.cpp)
    target_include_directories(cna_depth_effect_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    target_compile_definitions(cna_depth_effect_demo PRIVATE
        CNA_DEPTH_EFFECT_DEMO_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/Content")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_depth_effect_demo PRIVATE
            -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group SHARP_RUNTIME)
    else()
        target_link_libraries(cna_depth_effect_demo PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_depth_effect_demo PRIVATE SDL3::SDL3main)
    endif()
    if(WIN32)
        set_target_properties(cna_depth_effect_demo PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_depth_effect_demo)
    endif()
endif()

# ---- CRTEffect manual verification demo (renders + screenshots each CRT parameter set) --------
if(CNA_BUILD_EXAMPLES AND CNA_NOXNA AND (CNA_GRAPHICS_BACKEND STREQUAL "OPENGLES" OR CNA_GRAPHICS_BACKEND STREQUAL "OPENGL33") AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_crt_effect_demo examples/crt_effect_demo_test.cpp)
    target_include_directories(cna_crt_effect_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    target_compile_definitions(cna_crt_effect_demo PRIVATE
        CNA_CRT_EFFECT_DEMO_CONTENT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_2d/Content")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_libraries(cna_crt_effect_demo PRIVATE
            -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group SHARP_RUNTIME)
    else()
        target_link_libraries(cna_crt_effect_demo PRIVATE CNA SHARP_RUNTIME)
    endif()
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_crt_effect_demo PRIVATE SDL3::SDL3main)
    endif()
    if(WIN32)
        set_target_properties(cna_crt_effect_demo PROPERTIES WIN32_EXECUTABLE TRUE)
        cna_copy_sdl_runtime(cna_crt_effect_demo)
    endif()
endif()

# ---- Task 479: CNA-side reference-value dump tool (no window/GraphicsDevice needed) -----------
# Dumps enums, state presets, PackedVector, and Viewport reference values as JSON, mirroring
# tools/fna-reference/*.cs exactly so scripts/compare-fna-reference.py can diff the two outputs.
# Not registered as a ctest: comparing against the FNA-side harness additionally requires mono/
# xbuild and a locally-built FNA.dll (tools/fna-reference/README.md), which isn't guaranteed on
# every machine that builds CNA -- this is a manually-invoked developer verification tool.
if(CNA_BUILD_EXAMPLES AND NOT EMSCRIPTEN AND NOT ANDROID)
    add_executable(cna_reference_dump tools/cna-reference/CnaReferenceDump.cpp)
    target_link_libraries(cna_reference_dump PRIVATE CNA)
    if(TARGET SDL3::SDL3main)
        target_link_libraries(cna_reference_dump PRIVATE SDL3::SDL3main)
    endif()
endif()

if(CNA_BUILD_EXAMPLES AND (CNA_GRAPHICS_BACKEND STREQUAL "OPENGLES" OR CNA_GRAPHICS_BACKEND STREQUAL "OPENGL33"
        OR CNA_GRAPHICS_BACKEND STREQUAL "WEBGL1" OR CNA_GRAPHICS_BACKEND STREQUAL "WEBGL2"
        OR CNA_GRAPHICS_BACKEND STREQUAL "VULKAN"))
    # ---- 3D house demo (EasyGL and Vulkan) -----------------------------------
    add_executable(cna_house3d_demo examples/house3d_demo.cpp)

    if(EMSCRIPTEN)
        # ---- Emscripten / WebAssembly ----------------------------------------
        # Produce a self-contained HTML page. The scene is fully procedural so
        # no asset preloading is required.
        set_target_properties(cna_house3d_demo PROPERTIES SUFFIX ".html")
        # wasm-ld resolves circular static-lib references without linker groups.
        target_link_libraries(cna_house3d_demo PRIVATE
            CNA ${BACKEND_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        # plan_glbackends.md GLB-9 (revisited): MIN_WEBGL_VERSION/MAX_WEBGL_VERSION must match
        # whichever GL profile this target is actually compiled for -- pre-GLB-9 this was
        # hardcoded to WebGL 2 unconditionally, which would have forced a WebGL 2 context even
        # under -DCNA_GRAPHICS_BACKEND=WEBGL1 (contradicting EasyGLGraphicsBackend's own
        # SDL_GL_CONTEXT_MAJOR_VERSION=2 request for that profile, GLB-8) -- found while verifying
        # GLB-36's real emcc WEBGL1 build.
        if(CNA_GRAPHICS_BACKEND STREQUAL "WEBGL1")
            set(_cna_house3d_min_webgl_version 1)
            set(_cna_house3d_max_webgl_version 1)
        else()
            set(_cna_house3d_min_webgl_version 2)
            set(_cna_house3d_max_webgl_version 2)
        endif()
        target_link_options(cna_house3d_demo PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864       # 64 MB — enough for a procedural scene
            -sFORCE_FILESYSTEM=1
            "-sMIN_WEBGL_VERSION=${_cna_house3d_min_webgl_version}"
            "-sMAX_WEBGL_VERSION=${_cna_house3d_max_webgl_version}"
        )
    else()
        # ---- Native desktop --------------------------------------------------
        # The CNA library and the EasyGL backend static lib have circular symbol
        # references (backend uses Color/Rectangle from CNA, CNA uses backend
        # interfaces); wrap them in a linker group so GNU ld resolves both.
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_house3d_demo PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_house3d_demo PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_house3d_demo PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_house3d_demo PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_house3d_demo)
        endif()
    endif()

    # Task 11.11 (Phase 11b): real windowed proof that AvatarRenderer's real-rendering
    # extension (Phase 10) draws Phase 11a's procedurally-generated, non-synthetic avatar
    # content (tools/avatar_builder/ + tools/avatar_asset_pipeline/convert_avatar.py,
    # Task 11.10). Needs CNA_GamerServices (AvatarRenderer lives there, same gating as
    # cna_test_avatar_real_render below), already inside this EASYGL/VULKAN-only scope.
    # Emscripten isn't wired up for this demo yet (deferred, no immediate need).
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar
            examples/demo_avatar/src/Main.cpp
            examples/demo_avatar/src/AvatarDemo.cpp
        )
        target_include_directories(cna_demo_avatar PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/src
        )
        target_link_libraries(cna_demo_avatar PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar>/Content"
            COMMENT "Copying Content next to cna_demo_avatar executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar)
        endif()
    endif()

    # Task 15.1: cna_demo_net_client_server_arena - real two-process NetworkSession::Create/
    # Find/Join, LocalNetworkGamer::SendData/ReceiveData via PacketWriter/PacketReader, and
    # GamerJoined/SessionEnded events over real ENet, rendered as a small 2D arena. Needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_net_client_server_arena
            examples/demo_net_client_server_arena/src/Main.cpp
            examples/demo_net_client_server_arena/src/ArenaGame.cpp
        )
        target_include_directories(cna_demo_net_client_server_arena PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_net_client_server_arena/src
        )
        target_link_libraries(cna_demo_net_client_server_arena PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_net_client_server_arena PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_net_client_server_arena)
        endif()
    endif()

    # Task 15.2: cna_demo_packet_roundtrip - every XNA-type PacketWriter::Write/PacketReader::Read
    # overload (Vector2/3/4, Matrix, Quaternion, Color, float, double), single process, console-only,
    # no networking or windowing at all - needs only CNA_Net.
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_packet_roundtrip
            examples/demo_packet_roundtrip/src/Main.cpp
        )
        target_link_libraries(cna_demo_packet_roundtrip PRIVATE CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.3: cna_demo_qos_probe - QualityOfService/NetworkGamer::RoundtripTime measured
    # between two real gamers over real ENet. Console-only, no windowing - needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession/QualityOfService).
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_qos_probe
            examples/demo_qos_probe/src/Main.cpp
        )
        target_link_libraries(cna_demo_qos_probe PRIVATE CNA_GamerServices CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.4: cna_demo_simulated_network_conditions - NetworkSession::SimulatedLatencyProperty/
    # SimulatedPacketLossProperty over a real two-process ENet session, rendered as a small
    # Pong-style match. Needs CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same
    # gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_simulated_network_conditions
            examples/demo_simulated_network_conditions/src/Main.cpp
            examples/demo_simulated_network_conditions/src/SimGame.cpp
        )
        target_include_directories(cna_demo_simulated_network_conditions PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_simulated_network_conditions/src
        )
        target_link_libraries(cna_demo_simulated_network_conditions PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_simulated_network_conditions PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_simulated_network_conditions)
        endif()
    endif()

    # Task 15.5: cna_demo_session_browser - NetworkSession::Find(...)/AvailableNetworkSessionCollection
    # rendered as a scrollable session-lobby list, Up/Down to select, Enter to Join. Needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same gating as
    # cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_session_browser
            examples/demo_session_browser/src/Main.cpp
            examples/demo_session_browser/src/BrowserGame.cpp
        )
        target_include_directories(cna_demo_session_browser PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_session_browser/src
        )
        target_link_libraries(cna_demo_session_browser PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_session_browser PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_session_browser PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_session_browser PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_session_browser PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_session_browser)
        endif()
    endif()

    # Task 15.6: cna_demo_gamer_roster_hud - the full gamer-roster event surface (GamerJoined/
    # GamerLeft/HostChanged/SessionEnded) plus per-gamer IsHost/IsLocal/IsReady/IsTalking flags,
    # rendered as a live roster panel. Needs CNA_GamerServices (SignedInGamer) and CNA_Net
    # (NetworkSession) - same gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamer_roster_hud
            examples/demo_gamer_roster_hud/src/Main.cpp
            examples/demo_gamer_roster_hud/src/RosterGame.cpp
        )
        target_include_directories(cna_demo_gamer_roster_hud PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamer_roster_hud/src
        )
        target_link_libraries(cna_demo_gamer_roster_hud PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamer_roster_hud PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamer_roster_hud)
        endif()
    endif()

    # Task 15.7: cna_demo_session_lifecycle_events - NetworkSession::StartGame()/EndGame(),
    # NetworkSessionState transitions, GameStarted/GameEnded, and a manual Raise() of the 3
    # leaderboard delegates. Console-only, single process, NetworkSessionType::Local (no real
    # networking) - needs CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession).
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_session_lifecycle_events
            examples/demo_session_lifecycle_events/src/Main.cpp
        )
        target_link_libraries(cna_demo_session_lifecycle_events PRIVATE CNA_GamerServices CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.8: cna_demo_gamerservices_signin_presence - real GamerServicesComponent
    # registration, Gamer::SignedInGamers population, SignedInGamer::SignedIn/SignedOut, and
    # GamerPresence. Needs CNA_GamerServices (SignedInGamer/GamerServicesComponent) and windowing
    # (SpriteBatch/SpriteFont HUD) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamerservices_signin_presence
            examples/demo_gamerservices_signin_presence/src/Main.cpp
            examples/demo_gamerservices_signin_presence/src/PresenceGame.cpp
        )
        target_include_directories(cna_demo_gamerservices_signin_presence PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamerservices_signin_presence/src
        )
        target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamerservices_signin_presence PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamerservices_signin_presence)
        endif()
    endif()

    # Task 15.9: cna_demo_achievement_showcase - Achievement/AchievementCollection,
    # SignedInGamer::AwardAchievement/GetAchievements. Needs CNA_GamerServices and windowing
    # (SpriteBatch/SpriteFont HUD) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_achievement_showcase
            examples/demo_achievement_showcase/src/Main.cpp
            examples/demo_achievement_showcase/src/AchievementGame.cpp
        )
        target_include_directories(cna_demo_achievement_showcase PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_achievement_showcase/src
        )
        target_link_libraries(cna_demo_achievement_showcase PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_achievement_showcase PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_achievement_showcase PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_achievement_showcase PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_achievement_showcase PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_achievement_showcase)
        endif()
    endif()

    # Task 15.10: cna_demo_leaderboard_viewer - LeaderboardReader (CanPageUp/Down, Entries,
    # PageStart) plus LeaderboardWriter::GetLeaderboard's always-throws boundary. Needs
    # CNA_GamerServices and windowing (SpriteBatch/SpriteFont HUD) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_leaderboard_viewer
            examples/demo_leaderboard_viewer/src/Main.cpp
            examples/demo_leaderboard_viewer/src/LeaderboardGame.cpp
        )
        target_include_directories(cna_demo_leaderboard_viewer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_leaderboard_viewer/src
        )
        target_link_libraries(cna_demo_leaderboard_viewer PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_leaderboard_viewer PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_leaderboard_viewer)
        endif()
    endif()

    # Task 15.11: cna_demo_guide_overlay_console - the full Guide static API surface via a
    # numbered console menu. Console-only, single process, no graphics/window needed - needs only
    # CNA_GamerServices.
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_guide_overlay_console
            examples/demo_guide_overlay_console/src/Main.cpp
        )
        target_link_libraries(cna_demo_guide_overlay_console PRIVATE CNA_GamerServices SHARP_RUNTIME)
    endif()

    # Task 15.12: cna_demo_gamerservices_dispatcher_watchdog - visual proof that Task 12.1's/Task
    # 7.1's historical GamerServicesDispatcher/NetworkSession/GetAchievements hangs are fixed.
    # Needs CNA_GamerServices and CNA_Net, plus windowing for the "waiting.../SUCCESS" HUD - same
    # gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamerservices_dispatcher_watchdog
            examples/demo_gamerservices_dispatcher_watchdog/src/Main.cpp
            examples/demo_gamerservices_dispatcher_watchdog/src/WatchdogGame.cpp
        )
        target_include_directories(cna_demo_gamerservices_dispatcher_watchdog PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamerservices_dispatcher_watchdog/src
        )
        target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamerservices_dispatcher_watchdog PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamerservices_dispatcher_watchdog)
        endif()
    endif()

    # Task 15.13: cna_demo_gamer_profile_privileges - GamerProfile/GamerPrivileges via
    # Gamer::GetProfile()/SignedInGamer::getPrivilegesProperty(). Needs CNA_GamerServices and
    # windowing - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamer_profile_privileges
            examples/demo_gamer_profile_privileges/src/Main.cpp
            examples/demo_gamer_profile_privileges/src/ProfileGame.cpp
        )
        target_include_directories(cna_demo_gamer_profile_privileges PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_gamer_profile_privileges/src
        )
        target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_gamer_profile_privileges PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamer_profile_privileges)
        endif()
    endif()

    # Task 15.14: cna_demo_friends_and_gamercard - FriendCollection (via CreateInternal) and the
    # no-op Guide::ShowGamerCard/ShowFriendRequest/ShowFriends/ShowComposeMessage calls. Needs
    # CNA_GamerServices and windowing - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_friends_and_gamercard
            examples/demo_friends_and_gamercard/src/Main.cpp
            examples/demo_friends_and_gamercard/src/FriendsGame.cpp
        )
        target_include_directories(cna_demo_friends_and_gamercard PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_friends_and_gamercard/src
        )
        target_link_libraries(cna_demo_friends_and_gamercard PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            set_target_properties(cna_demo_friends_and_gamercard PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_friends_and_gamercard)
        endif()
    endif()

    # Task 15.15: cna_demo_avatar_animation_gallery - a completionist version of demo_avatar's
    # Space-cycling, iterating all 31 AvatarAnimationPreset values across both genders. Reuses
    # demo_avatar's Content/ directory (copied by CMake, not duplicated) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_animation_gallery
            examples/demo_avatar_animation_gallery/src/Main.cpp
            examples/demo_avatar_animation_gallery/src/GalleryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_animation_gallery PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_animation_gallery/src
        )
        target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_animation_gallery POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_animation_gallery>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_animation_gallery executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_animation_gallery PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_animation_gallery)
        endif()
    endif()

    # Task 15.16: cna_demo_avatar_wardrobe_hotswap - SkinnedModelEXT::AttachPartEXT/RemovePartEXT
    # used repeatedly at runtime (Tab cycles baked-in hair / hair_Cap / hair_Ponytail). Reuses
    # demo_avatar's Content/ directory (copied by CMake, not duplicated) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_wardrobe_hotswap
            examples/demo_avatar_wardrobe_hotswap/src/Main.cpp
            examples/demo_avatar_wardrobe_hotswap/src/HotswapDemo.cpp
        )
        target_include_directories(cna_demo_avatar_wardrobe_hotswap PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_wardrobe_hotswap/src
        )
        target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_wardrobe_hotswap POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_wardrobe_hotswap>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_wardrobe_hotswap executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_wardrobe_hotswap PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_wardrobe_hotswap)
        endif()
    endif()

    # Task 15.17: cna_demo_avatar_appearance_tint_studio - AvatarAppearanceEXT's 5 tint slots and
    # AvatarRenderer::SetAppearanceEXT as a live color customization screen. Reuses demo_avatar's
    # Content/ directory (copied by CMake, not duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_appearance_tint_studio
            examples/demo_avatar_appearance_tint_studio/src/Main.cpp
            examples/demo_avatar_appearance_tint_studio/src/TintStudioDemo.cpp
        )
        target_include_directories(cna_demo_avatar_appearance_tint_studio PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_appearance_tint_studio/src
        )
        target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_appearance_tint_studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_appearance_tint_studio>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_appearance_tint_studio executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_appearance_tint_studio PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_appearance_tint_studio)
        endif()
    endif()

    # Task 15.18: cna_demo_avatar_dual_compare - two independent AvatarRenderer/SkinnedModelEXT
    # instances alive and drawing simultaneously. Reuses demo_avatar's Content/ directory (copied
    # by CMake, not duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_dual_compare
            examples/demo_avatar_dual_compare/src/Main.cpp
            examples/demo_avatar_dual_compare/src/DualCompareDemo.cpp
        )
        target_include_directories(cna_demo_avatar_dual_compare PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_dual_compare/src
        )
        target_link_libraries(cna_demo_avatar_dual_compare PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_dual_compare POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_dual_compare>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_dual_compare executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_dual_compare PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_dual_compare)
        endif()
    endif()

    # Task 15.19: cna_demo_avatar_multi_attach_stress - SkinnedModelEXT::AttachPartEXT called
    # repeatedly (hair variants, then a growing series of synthetic quad accessories), proving
    # accumulation doesn't break skinning/tinting as Parts.size() grows. Reuses demo_avatar's
    # Content/ directory (copied by CMake, not duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_multi_attach_stress
            examples/demo_avatar_multi_attach_stress/src/Main.cpp
            examples/demo_avatar_multi_attach_stress/src/StressDemo.cpp
        )
        target_include_directories(cna_demo_avatar_multi_attach_stress PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_multi_attach_stress/src
        )
        target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_multi_attach_stress POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_multi_attach_stress>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_multi_attach_stress executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_multi_attach_stress PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_multi_attach_stress)
        endif()
    endif()

    # Task 15.20: cna_demo_avatar_bone_state_boundary - documents the real, verified
    # AvatarRenderer skeleton-API boundary (State/ParentBones/BindPose) vs the working
    # SkinnedModelEXT EXT path. Reuses demo_avatar's Content/ directory (copied by CMake, not
    # duplicated) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_bone_state_boundary
            examples/demo_avatar_bone_state_boundary/src/Main.cpp
            examples/demo_avatar_bone_state_boundary/src/BoundaryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_bone_state_boundary PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar_bone_state_boundary/src
        )
        target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_bone_state_boundary POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_bone_state_boundary>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_bone_state_boundary executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_bone_state_boundary PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_bone_state_boundary)
        endif()
    endif()

    # Task 15.21 (bonus, cross-cutting): cna_demo_net_avatar_sync - combines Net + Avatar over a
    # real two-process ENet session, each side rendering both its own and the remote peer's real
    # avatar. Needs CNA_GamerServices/CNA_Net (real networking) plus demo_avatar's Content/
    # directory (copied by CMake, not duplicated) - same gating as cna_demo_net_client_server_arena
    # above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_net_avatar_sync
            examples/demo_net_avatar_sync/src/Main.cpp
            examples/demo_net_avatar_sync/src/SyncGame.cpp
        )
        target_include_directories(cna_demo_net_avatar_sync PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_net_avatar_sync/src
        )
        target_link_libraries(cna_demo_net_avatar_sync PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE
                -Wl,--start-group CNA ${BACKEND_TARGET} -Wl,--end-group
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_net_avatar_sync POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_net_avatar_sync>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_net_avatar_sync executable")
        if(WIN32)
            set_target_properties(cna_demo_net_avatar_sync PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_net_avatar_sync)
        endif()
    endif()
endif()
