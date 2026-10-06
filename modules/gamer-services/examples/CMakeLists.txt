# plans/plan_runtimerenderer.md RTR-P9-13/P9-15 -- the rule these gates follow, corrected after getting it
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

if(CNA_BUILD_EXAMPLES AND (CNA_GRAPHICS_RENDERER STREQUAL "OPENGLES3"
        OR CNA_GRAPHICS_RENDERER STREQUAL "OPENGL33" OR CNA_GRAPHICS_RENDERER STREQUAL "WEBGL2"
        OR CNA_GRAPHICS_RENDERER STREQUAL "VULKAN"))
    # Every demo below links CNA ahead of CNA_GamerServices: in a shared CNA build, the static module
    # archives CNA_GamerServices pulls in must not supply a second copy of the runtime (and of the GL
    # loader state) ahead of libcna, which aborted every one of these demos at device creation.
    # GS-009: the standard XNA avatar demos. They use only AvatarDescription/AvatarAnimation/
    # AvatarRenderer, whose original CNA catalog is compiled into CNA_GamerServices, so no content
    # is copied. Emscripten isn't wired up for these demos yet.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar
            demo_avatar/src/Main.cpp
            demo_avatar/src/AvatarDemo.cpp
        )
        target_include_directories(cna_demo_avatar PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar PRIVATE CNA CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_avatar PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_avatar)
            cna_copy_sdl_runtime(cna_demo_avatar)
        endif()
    endif()

    # Task 15.8: cna_demo_gamerservices_signin_presence - real GamerServicesComponent
    # registration, Gamer::SignedInGamers population, SignedInGamer::SignedIn/SignedOut, and
    # GamerPresence. Needs CNA_GamerServices (SignedInGamer/GamerServicesComponent) and windowing
    # (SpriteBatch/SpriteFont HUD) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamerservices_signin_presence
            demo_gamerservices_signin_presence/src/Main.cpp
            demo_gamerservices_signin_presence/src/PresenceGame.cpp
        )
        target_include_directories(cna_demo_gamerservices_signin_presence PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_gamerservices_signin_presence/src
        )
        target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE CNA CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_gamerservices_signin_presence)
            cna_copy_sdl_runtime(cna_demo_gamerservices_signin_presence)
        endif()
    endif()

    # Task 15.9: cna_demo_achievement_showcase - Achievement/AchievementCollection,
    # SignedInGamer::AwardAchievement/GetAchievements. Needs CNA_GamerServices and windowing
    # (SpriteBatch/SpriteFont HUD) - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_achievement_showcase
            demo_achievement_showcase/src/Main.cpp
            demo_achievement_showcase/src/AchievementGame.cpp
        )
        target_include_directories(cna_demo_achievement_showcase PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_achievement_showcase/src
        )
        target_link_libraries(cna_demo_achievement_showcase PRIVATE CNA CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_achievement_showcase PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_achievement_showcase PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_achievement_showcase PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_achievement_showcase)
            cna_copy_sdl_runtime(cna_demo_achievement_showcase)
        endif()
    endif()

    # Task 15.10: cna_demo_leaderboard_viewer - LeaderboardReader (CanPageUp/Down, Entries,
    # PageStart) over ratings written through LeaderboardWriter::GetLeaderboard. Needs
    # CNA_GamerServices and windowing (SpriteBatch/SpriteFont HUD) - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_leaderboard_viewer
            demo_leaderboard_viewer/src/Main.cpp
            demo_leaderboard_viewer/src/LeaderboardGame.cpp
        )
        target_include_directories(cna_demo_leaderboard_viewer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_leaderboard_viewer/src
        )
        target_link_libraries(cna_demo_leaderboard_viewer PRIVATE CNA CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_leaderboard_viewer PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_leaderboard_viewer)
            cna_copy_sdl_runtime(cna_demo_leaderboard_viewer)
        endif()
    endif()

    # Task 15.11: cna_demo_guide_overlay_console - Guide static API calls via a
    # numbered console menu. Console-only, single process, no graphics/window needed - needs only
    # CNA_GamerServices.
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_guide_overlay_console
            demo_guide_overlay_console/src/Main.cpp
        )
        target_link_libraries(cna_demo_guide_overlay_console PRIVATE CNA CNA_GamerServices SHARP_RUNTIME)
    endif()

    # Task 15.12: cna_demo_gamerservices_dispatcher_watchdog - visual proof that Task 12.1's/Task
    # 7.1's historical GamerServicesDispatcher/NetworkSession/GetAchievements hangs are fixed.
    # Needs CNA_GamerServices and CNA_Net, plus windowing for the "waiting.../SUCCESS" HUD - same
    # gating as cna_demo_net_client_server_arena in the net module's examples.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamerservices_dispatcher_watchdog
            demo_gamerservices_dispatcher_watchdog/src/Main.cpp
            demo_gamerservices_dispatcher_watchdog/src/WatchdogGame.cpp
        )
        target_include_directories(cna_demo_gamerservices_dispatcher_watchdog PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_gamerservices_dispatcher_watchdog/src
        )
        target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE CNA CNA_GamerServices CNA_Net)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_gamerservices_dispatcher_watchdog)
            cna_copy_sdl_runtime(cna_demo_gamerservices_dispatcher_watchdog)
        endif()
    endif()

    # Task 15.13: cna_demo_gamer_profile_privileges - GamerProfile/GamerPrivileges via
    # Gamer::GetProfile()/SignedInGamer::getPrivilegesProperty(). Needs CNA_GamerServices and
    # windowing - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_gamer_profile_privileges
            demo_gamer_profile_privileges/src/Main.cpp
            demo_gamer_profile_privileges/src/ProfileGame.cpp
        )
        target_include_directories(cna_demo_gamer_profile_privileges PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_gamer_profile_privileges/src
        )
        target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE CNA CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_gamer_profile_privileges)
            cna_copy_sdl_runtime(cna_demo_gamer_profile_privileges)
        endif()
    endif()

    # Task 15.14: cna_demo_friends_and_gamercard - FriendCollection (via CreateInternal) and the
    # Guide::ShowGamerCard/ShowFriendRequest/ShowFriends/ShowComposeMessage service panes, which
    # refuse without a CNA account service. Needs CNA_GamerServices and windowing - same gating as
    # cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_friends_and_gamercard
            demo_friends_and_gamercard/src/Main.cpp
            demo_friends_and_gamercard/src/FriendsGame.cpp
        )
        target_include_directories(cna_demo_friends_and_gamercard PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_friends_and_gamercard/src
        )
        target_link_libraries(cna_demo_friends_and_gamercard PRIVATE CNA CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_friends_and_gamercard PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_friends_and_gamercard)
            cna_copy_sdl_runtime(cna_demo_friends_and_gamercard)
        endif()
    endif()

    # Task 15.15 / GS-009: cna_demo_avatar_animation_gallery - eight random avatars playing all 31
    # AvatarAnimationPreset values a page at a time - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_animation_gallery
            demo_avatar_animation_gallery/src/Main.cpp
            demo_avatar_animation_gallery/src/GalleryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_animation_gallery PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar_animation_gallery/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE CNA CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_avatar_animation_gallery)
            cna_copy_sdl_runtime(cna_demo_avatar_animation_gallery)
        endif()
    endif()

    # Task 15.18 / GS-009: cna_demo_avatar_dual_compare - a random female and male avatar side by
    # side on the same preset - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_dual_compare
            demo_avatar_dual_compare/src/Main.cpp
            demo_avatar_dual_compare/src/DualCompareDemo.cpp
        )
        target_include_directories(cna_demo_avatar_dual_compare PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar_dual_compare/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar_dual_compare PRIVATE CNA CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_dual_compare PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_avatar_dual_compare)
            cna_copy_sdl_runtime(cna_demo_avatar_dual_compare)
        endif()
    endif()

    # Task 15.20 / GS-009: cna_demo_avatar_bone_state_boundary - AvatarRenderer state, ParentBones
    # and BindPose, then game-posed custom bones - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar_bone_state_boundary
            demo_avatar_bone_state_boundary/src/Main.cpp
            demo_avatar_bone_state_boundary/src/BoundaryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_bone_state_boundary PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar_bone_state_boundary/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE CNA CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_demo_avatar_bone_state_boundary)
            cna_copy_sdl_runtime(cna_demo_avatar_bone_state_boundary)
        endif()
    endif()

    # GSP-A2: cna_avatar_review renders tools/avatar_builder/avatar_review.py job lists through the
    # standard avatar API, for deterministic before/after review sheets.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_avatar_review avatar_review/AvatarReview.cpp)
        target_link_libraries(cna_avatar_review PRIVATE CNA CNA_GamerServices SHARP_RUNTIME)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_avatar_review PRIVATE SDL3::SDL3main)
        endif()
        # GSX-U0: deterministic captures of every system UI surface over a stand-in game.
        add_executable(cna_guide_review guide_review/GuideReview.cpp)
        target_link_libraries(cna_guide_review PRIVATE CNA CNA_GamerServices SHARP_RUNTIME)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_guide_review PRIVATE SDL3::SDL3main)
        endif()
        # GSP-I1: the CNA avatar editor, a program of its own so no game needs customization code.
        add_executable(cna_avatar_editor avatar_editor/AvatarEditor.cpp)
        target_link_libraries(cna_avatar_editor PRIVATE CNA CNA_GamerServices SHARP_RUNTIME)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_avatar_editor PRIVATE SDL3::SDL3main)
        endif()
        if(WIN32)
            cna_windows_gui_executable(cna_avatar_editor)
            cna_copy_sdl_runtime(cna_avatar_editor)
        endif()
    endif()
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
