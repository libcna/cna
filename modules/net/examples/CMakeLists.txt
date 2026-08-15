# plan_runtimerenderer.md RTR-P9-13/P9-15 -- the rule these gates follow, corrected after getting it
# wrong once:
#
#   * A gate deciding whether a TARGET EXISTS, where that target runs against whatever renderer the
#     build DEFAULTS to, must test equality with CNA_GRAPHICS_RENDERER. Membership is wrong there: a
#     multi-renderer build defaulting to SOFTWARE but containing OPENGLES3 would build a demo that
#     needs 3D and then throw "3D not supported" at runtime -- a build that succeeds and a program
#     that cannot work.
#   * A gate deciding whether a RESOURCE is available -- a runtime library to copy, a renderer's own
#     device-free test sources -- may test membership, because the user really can select that
#     renderer at runtime in a multi build.
#
# In single-renderer mode the two are identical, which is exactly why the distinction is easy to
# miss.


# Example/demo executables historically live at the build root; keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(CNA_BUILD_EXAMPLES AND (CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES2" OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
        OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33"
        OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL1" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2"
        OR CNA_GRAPHICS_RENDERER STREQUAL "VULKAN"))
    # Task 15.1: cna_demo_net_client_server_arena - real two-process NetworkSession::Create/
    # Find/Join, LocalNetworkGamer::SendData/ReceiveData via PacketWriter/PacketReader, and
    # GamerJoined/SessionEnded events over real ENet, rendered as a small 2D arena. Needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same gating as
    # cna_demo_avatar in the gamer-services module's examples.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_net_client_server_arena
            demo_net_client_server_arena/src/Main.cpp
            demo_net_client_server_arena/src/ArenaGame.cpp
        )
        target_include_directories(cna_demo_net_client_server_arena PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_net_client_server_arena/src
        )
        target_link_libraries(cna_demo_net_client_server_arena PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_net_client_server_arena PRIVATE
                CNA
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
            demo_packet_roundtrip/src/Main.cpp
        )
        target_link_libraries(cna_demo_packet_roundtrip PRIVATE CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.3: cna_demo_qos_probe - QualityOfService/NetworkGamer::RoundtripTime measured
    # between two real gamers over real ENet. Console-only, no windowing - needs
    # CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession/QualityOfService).
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_qos_probe
            demo_qos_probe/src/Main.cpp
        )
        target_link_libraries(cna_demo_qos_probe PRIVATE CNA_GamerServices CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.4: cna_demo_simulated_network_conditions - NetworkSession::SimulatedLatencyProperty/
    # SimulatedPacketLossProperty over a real two-process ENet session, rendered as a small
    # Pong-style match. Needs CNA_GamerServices (SignedInGamer) and CNA_Net (NetworkSession) - same
    # gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_simulated_network_conditions
            demo_simulated_network_conditions/src/Main.cpp
            demo_simulated_network_conditions/src/SimGame.cpp
        )
        target_include_directories(cna_demo_simulated_network_conditions PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_simulated_network_conditions/src
        )
        target_link_libraries(cna_demo_simulated_network_conditions PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_simulated_network_conditions PRIVATE
                CNA
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
            demo_session_browser/src/Main.cpp
            demo_session_browser/src/BrowserGame.cpp
        )
        target_include_directories(cna_demo_session_browser PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_session_browser/src
        )
        target_link_libraries(cna_demo_session_browser PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_session_browser PRIVATE
                CNA
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
            demo_gamer_roster_hud/src/Main.cpp
            demo_gamer_roster_hud/src/RosterGame.cpp
        )
        target_include_directories(cna_demo_gamer_roster_hud PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_gamer_roster_hud/src
        )
        target_link_libraries(cna_demo_gamer_roster_hud PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_gamer_roster_hud PRIVATE
                CNA
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
            demo_session_lifecycle_events/src/Main.cpp
        )
        target_link_libraries(cna_demo_session_lifecycle_events PRIVATE CNA_GamerServices CNA_Net SHARP_RUNTIME)
    endif()

    # Task 15.21 (bonus, cross-cutting): cna_demo_net_avatar_sync - combines Net + Avatar over a
    # real two-process ENet session, each side rendering both its own and the remote peer's real
    # avatar. Needs CNA_GamerServices/CNA_Net (real networking) plus demo_avatar's Content/
    # directory (copied by CMake from the gamer-services module's examples, not duplicated) -
    # same gating as cna_demo_net_client_server_arena above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_net_avatar_sync
            demo_net_avatar_sync/src/Main.cpp
            demo_net_avatar_sync/src/SyncGame.cpp
        )
        target_include_directories(cna_demo_net_avatar_sync PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_net_avatar_sync/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_net_avatar_sync PRIVATE CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_net_avatar_sync PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_net_avatar_sync POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/modules/gamer-services/examples/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_net_avatar_sync>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_net_avatar_sync executable")
        if(WIN32)
            set_target_properties(cna_demo_net_avatar_sync PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_net_avatar_sync)
        endif()
    endif()
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
