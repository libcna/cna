# Devices-subsystem examples (module-local registration; the target definitions moved here
# verbatim from cmake/Examples.cmake when examples moved into their owning physical modules).

# Example/demo executables historically live at the build root; keep them there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---- Devices (Sensors/VibrateController) demo (works with all renderers) --
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
# demo_devices/android/ (docs/devices-build.md Section 4.1),
# which compiles Main.cpp/DevicesDemo.cpp into a genuine shared library
# ("main") via its own app/jni/src/CMakeLists.txt -- not this target.
if(CNA_BUILD_EXAMPLES AND NOT ANDROID)
    add_executable(cna_demo_devices
        demo_devices/src/Main.cpp
        demo_devices/src/DevicesDemo.cpp
    )
    target_include_directories(cna_demo_devices PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/demo_devices/src
    )

    if(EMSCRIPTEN)
        set_target_properties(cna_demo_devices PROPERTIES SUFFIX ".html")
        target_link_libraries(cna_demo_devices PRIVATE
            CNA ${RENDERER_TARGET} SDL3::SDL3-static SHARP_RUNTIME)
        target_link_options(cna_demo_devices PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sSTACK_SIZE=1048576
            -sINITIAL_MEMORY=67108864
            -sFORCE_FILESYSTEM=1
        )
    else()
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32 AND NOT APPLE)
            target_link_libraries(cna_demo_devices PRIVATE
                CNA
                SHARP_RUNTIME)
        else()
            target_link_libraries(cna_demo_devices PRIVATE CNA SHARP_RUNTIME)
        endif()
        # demo_devices/src/Main.cpp includes <SDL3/SDL_main.h>
        # directly (needed on Android so SDLActivity.java's dlsym("SDL_main")
        # lookup finds a real symbol) -- unlike every other demo target,
        # which never touches an SDL3 header directly and only goes through
        # CNA's own abstraction layer. CNA links SDL3::SDL3 PRIVATE, so that
        # include path does not propagate transitively
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
endif()

# Module-local tests get the same headless-safe skip convention the root one-shot
# gives root-registered tests (see cmake/TestHelpers.cmake).
cna_apply_skip_convention()
