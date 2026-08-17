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
    # Task 11.11 (Phase 11b): real windowed proof that AvatarRenderer's real-rendering
    # extension (Phase 10) draws Phase 11a's procedurally-generated, non-synthetic avatar
    # content (tools/avatar_builder/ + tools/avatar_asset_pipeline/convert_avatar.py,
    # Task 11.10). Needs CNA_GamerServices (AvatarRenderer lives there, same gating as
    # cna_test_avatar_real_render below), already inside this EASYGL/VULKAN-only scope.
    # Emscripten isn't wired up for this demo yet (deferred, no immediate need).
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_avatar
            demo_avatar/src/Main.cpp
            demo_avatar/src/AvatarDemo.cpp
        )
        target_include_directories(cna_demo_avatar PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar PRIVATE CNA_GamerServices)

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
        add_custom_command(TARGET cna_demo_avatar POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar>/Content"
            COMMENT "Copying Content next to cna_demo_avatar executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar PROPERTIES WIN32_EXECUTABLE TRUE)
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
        target_link_libraries(cna_demo_gamerservices_signin_presence PRIVATE CNA_GamerServices)

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
            set_target_properties(cna_demo_gamerservices_signin_presence PROPERTIES WIN32_EXECUTABLE TRUE)
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
        target_link_libraries(cna_demo_achievement_showcase PRIVATE CNA_GamerServices)

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
            demo_leaderboard_viewer/src/Main.cpp
            demo_leaderboard_viewer/src/LeaderboardGame.cpp
        )
        target_include_directories(cna_demo_leaderboard_viewer PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_leaderboard_viewer/src
        )
        target_link_libraries(cna_demo_leaderboard_viewer PRIVATE CNA_GamerServices)

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
            set_target_properties(cna_demo_leaderboard_viewer PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_leaderboard_viewer)
        endif()
    endif()

    # Task 15.11: cna_demo_guide_overlay_console - the full Guide static API surface via a
    # numbered console menu. Console-only, single process, no graphics/window needed - needs only
    # CNA_GamerServices.
    if(CNA_ENABLE_NET)
        add_executable(cna_demo_guide_overlay_console
            demo_guide_overlay_console/src/Main.cpp
        )
        target_link_libraries(cna_demo_guide_overlay_console PRIVATE CNA_GamerServices SHARP_RUNTIME)
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
        target_link_libraries(cna_demo_gamerservices_dispatcher_watchdog PRIVATE CNA_GamerServices CNA_Net)

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
            set_target_properties(cna_demo_gamerservices_dispatcher_watchdog PROPERTIES WIN32_EXECUTABLE TRUE)
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
        target_link_libraries(cna_demo_gamer_profile_privileges PRIVATE CNA_GamerServices)

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
            set_target_properties(cna_demo_gamer_profile_privileges PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_gamer_profile_privileges)
        endif()
    endif()

    # Task 15.14: cna_demo_friends_and_gamercard - FriendCollection (via CreateInternal) and the
    # no-op Guide::ShowGamerCard/ShowFriendRequest/ShowFriends/ShowComposeMessage calls. Needs
    # CNA_GamerServices and windowing - same gating as cna_demo_avatar above.
    if(CNA_ENABLE_NET AND NOT EMSCRIPTEN)
        add_executable(cna_demo_friends_and_gamercard
            demo_friends_and_gamercard/src/Main.cpp
            demo_friends_and_gamercard/src/FriendsGame.cpp
        )
        target_include_directories(cna_demo_friends_and_gamercard PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_friends_and_gamercard/src
        )
        target_link_libraries(cna_demo_friends_and_gamercard PRIVATE CNA_GamerServices)

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
            demo_avatar_animation_gallery/src/Main.cpp
            demo_avatar_animation_gallery/src/GalleryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_animation_gallery PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar_animation_gallery/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar_animation_gallery PRIVATE CNA_GamerServices)

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
        add_custom_command(TARGET cna_demo_avatar_animation_gallery POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar/Content"
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
            demo_avatar_wardrobe_hotswap/src/Main.cpp
            demo_avatar_wardrobe_hotswap/src/HotswapDemo.cpp
        )
        target_include_directories(cna_demo_avatar_wardrobe_hotswap PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar_wardrobe_hotswap/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_wardrobe_hotswap PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_wardrobe_hotswap POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar/Content"
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
            demo_avatar_appearance_tint_studio/src/Main.cpp
            demo_avatar_appearance_tint_studio/src/TintStudioDemo.cpp
        )
        target_include_directories(cna_demo_avatar_appearance_tint_studio PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar_appearance_tint_studio/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_appearance_tint_studio PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_appearance_tint_studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar/Content"
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
            demo_avatar_dual_compare/src/Main.cpp
            demo_avatar_dual_compare/src/DualCompareDemo.cpp
        )
        target_include_directories(cna_demo_avatar_dual_compare PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar_dual_compare/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar_dual_compare PRIVATE CNA_GamerServices)

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
        add_custom_command(TARGET cna_demo_avatar_dual_compare POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar/Content"
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
            demo_avatar_multi_attach_stress/src/Main.cpp
            demo_avatar_multi_attach_stress/src/StressDemo.cpp
        )
        target_include_directories(cna_demo_avatar_multi_attach_stress PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar_multi_attach_stress/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE CNA_GamerServices)

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE CNA SHARP_RUNTIME)
        endif()
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_demo_avatar_multi_attach_stress PRIVATE SDL3::SDL3main)
        endif()
        add_custom_command(TARGET cna_demo_avatar_multi_attach_stress POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar/Content"
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
            demo_avatar_bone_state_boundary/src/Main.cpp
            demo_avatar_bone_state_boundary/src/BoundaryDemo.cpp
        )
        target_include_directories(cna_demo_avatar_bone_state_boundary PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar_bone_state_boundary/src
            ${CNA_GRAPHICS_EXAMPLES_DIR}
        )
        target_link_libraries(cna_demo_avatar_bone_state_boundary PRIVATE CNA_GamerServices)

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
        add_custom_command(TARGET cna_demo_avatar_bone_state_boundary POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/demo_avatar/Content"
                "$<TARGET_FILE_DIR:cna_demo_avatar_bone_state_boundary>/Content"
            COMMENT "Copying demo_avatar's Content next to cna_demo_avatar_bone_state_boundary executable")
        if(WIN32)
            set_target_properties(cna_demo_avatar_bone_state_boundary PROPERTIES WIN32_EXECUTABLE TRUE)
            cna_copy_sdl_runtime(cna_demo_avatar_bone_state_boundary)
        endif()
    endif()
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
