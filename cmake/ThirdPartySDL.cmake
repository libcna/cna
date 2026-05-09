include_guard(GLOBAL)

option(CNA_USE_SYSTEM_SDL "Use system SDL packages instead of vendored submodules" OFF)

# Configure SDL3, SDL_image, and SDL_mixer either from vendored submodules
# (default) or from system packages (CNA_USE_SYSTEM_SDL=ON).
function(cna_configure_vendored_sdl)
    if(TARGET SDL3::SDL3 AND TARGET SDL3_image::SDL3_image AND TARGET SDL3_mixer::SDL3_mixer)
        return()
    endif()

    if(CNA_USE_SYSTEM_SDL)
        find_package(SDL3 REQUIRED)
        find_package(SDL3_image REQUIRED)
        find_package(SDL3_mixer REQUIRED)
        return()
    endif()

    set(_third_party_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    foreach(_dep IN ITEMS SDL SDL_image SDL_mixer)
        if(NOT EXISTS "${_third_party_root}/${_dep}/CMakeLists.txt")
            message(FATAL_ERROR
                "Missing vendored dependency '${_dep}' in ${_third_party_root}. "
                "Run: git submodule update --init --recursive")
        endif()
    endforeach()

    if(ANDROID)
        # Android requires libSDL3.so to be packaged inside the APK so the
        # Java SDL activity can load it via System.loadLibrary("SDL3").
        set(SDL_SHARED ON  CACHE BOOL "Build SDL as shared" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "Build SDL as static" FORCE)
    elseif(EMSCRIPTEN)
        # Emscripten only supports static / WASM output.
        set(SDL_SHARED OFF CACHE BOOL "Build SDL as shared" FORCE)
        set(SDL_STATIC ON  CACHE BOOL "Build SDL as static" FORCE)
    else()
        set(SDL_SHARED ON  CACHE BOOL "Build SDL as shared" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "Build SDL as static" FORCE)
    endif()

    set(SDL_TESTS    OFF CACHE BOOL "Build SDL tests"    FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Build SDL examples" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "Disable SDL install target in superbuild" FORCE)

    set(SDLIMAGE_VENDORED  ON  CACHE BOOL "Use vendored dependencies in SDL_image" FORCE)
    set(SDLIMAGE_TESTS     OFF CACHE BOOL "Build SDL_image tests"    FORCE)
    set(SDLIMAGE_SAMPLES   OFF CACHE BOOL "Build SDL_image samples"  FORCE)
    set(SDLIMAGE_INSTALL   OFF CACHE BOOL "Disable SDL_image install target in superbuild" FORCE)
    set(SDLIMAGE_AVIF      OFF CACHE BOOL "Disable AVIF support"   FORCE)
    set(SDLIMAGE_JXL       OFF CACHE BOOL "Disable JXL support"    FORCE)
    set(SDLIMAGE_TIF       OFF CACHE BOOL "Disable TIFF support"   FORCE)
    set(SDLIMAGE_WEBP      OFF CACHE BOOL "Disable WEBP support"   FORCE)
    set(SDLIMAGE_PNG_LIBPNG OFF CACHE BOOL "Use internal PNG loader instead of libpng" FORCE)

    set(SDLMIXER_VENDORED        ON  CACHE BOOL "Use vendored dependencies in SDL_mixer" FORCE)
    set(SDLMIXER_TESTS           OFF CACHE BOOL "Build SDL_mixer tests"    FORCE)
    set(SDLMIXER_EXAMPLES        OFF CACHE BOOL "Build SDL_mixer examples" FORCE)
    set(SDLMIXER_INSTALL         OFF CACHE BOOL "Disable SDL_mixer install target in superbuild" FORCE)
    set(SDLMIXER_GME             OFF CACHE BOOL "Disable GME support"           FORCE)
    set(SDLMIXER_MOD_XMP         OFF CACHE BOOL "Disable libxmp backend"        FORCE)
    set(SDLMIXER_MP3_MPG123      OFF CACHE BOOL "Disable mpg123 backend"        FORCE)
    set(SDLMIXER_MIDI_FLUIDSYNTH OFF CACHE BOOL "Disable FluidSynth backend"    FORCE)
    set(SDLMIXER_OPUS            OFF CACHE BOOL "Disable Opus support"          FORCE)
    set(SDLMIXER_VORBIS_VORBISFILE OFF CACHE BOOL "Disable libvorbisfile backend" FORCE)
    set(SDLMIXER_VORBIS_TREMOR   OFF CACHE BOOL "Disable tremor backend"        FORCE)
    set(SDLMIXER_WAVPACK         OFF CACHE BOOL "Disable WavPack support"       FORCE)
    set(SDLMIXER_FLAC_LIBFLAC    OFF CACHE BOOL "Disable libFLAC backend"       FORCE)

    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL       EXCLUDE_FROM_ALL)

    # After SDL3 has been configured via add_subdirectory, point SDL3_DIR at
    # its build-tree location so that SDL_image's internal find_package(SDL3)
    # finds the vendored copy instead of searching for a system-installed package.
    set(SDL3_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/SDL" CACHE PATH "" FORCE)

    # On Emscripten SDL is built as static-only (SDL_STATIC=ON, SDL_SHARED=OFF).
    # SDL_image uses BUILD_SHARED_LIBS to decide whether to look for
    # SDL3::SDL3-shared or SDL3::SDL3. Force it to OFF so SDL_image picks the
    # static path (SDL3::SDL3 → SDL3-static) which already exists.
    if(EMSCRIPTEN)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    endif()

    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_image EXCLUDE_FROM_ALL)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL_mixer EXCLUDE_FROM_ALL)
endfunction()

# Copy the MinGW threading runtime DLL (libwinpthread-1.dll) next to a target
# executable on Windows/MinGW builds.  libgcc_s_seh-1.dll and libstdc++-6.dll
# are handled by passing -static-libgcc / -static-libstdc++ at link time.
# No-op on non-MinGW / non-Windows / Emscripten / Android.
function(cna_copy_mingw_runtime target_name)
    if(NOT MINGW)
        return()
    endif()
    if(EMSCRIPTEN OR ANDROID)
        return()
    endif()

    # Ask GCC where it stores libwinpthread-1.dll at configure time.
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -print-file-name=libwinpthread-1.dll
        OUTPUT_VARIABLE _pthread_dll
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # gcc returns just the bare name when it cannot find the file (no path separator).
    if(_pthread_dll AND _pthread_dll MATCHES "[/\\\\]")
        # Normalise to CMake path (forward slashes).
        file(TO_CMAKE_PATH "${_pthread_dll}" _pthread_dll)
        if(EXISTS "${_pthread_dll}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_pthread_dll}"
                    $<TARGET_FILE_DIR:${target_name}>
                COMMENT "Copying libwinpthread-1.dll next to ${target_name}"
                VERBATIM)
        endif()
    else()
        # Fallback: search common MinGW bin directories relative to the compiler.
        get_filename_component(_mingw_bin "${CMAKE_C_COMPILER}" DIRECTORY)
        set(_pthread_fallback "${_mingw_bin}/libwinpthread-1.dll")
        if(EXISTS "${_pthread_fallback}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_pthread_fallback}"
                    $<TARGET_FILE_DIR:${target_name}>
                COMMENT "Copying libwinpthread-1.dll (fallback) next to ${target_name}"
                VERBATIM)
        else()
            message(WARNING
                "cna_copy_mingw_runtime: could not locate libwinpthread-1.dll. "
                "Searched: '${_pthread_dll}' and '${_pthread_fallback}'. "
                "The executable may fail to run on machines without MinGW installed.")
        endif()
    endif()
endfunction()

# Copy SDL shared libraries next to the target executable on Windows.
# No-op on Emscripten (static wasm), Android (APK packaging), and non-Windows platforms.
function(cna_copy_sdl_runtime target_name)
    if(EMSCRIPTEN)
        return()
    endif()
    if(ANDROID)
        return()
    endif()
    if(NOT WIN32)
        return()
    endif()
    foreach(_dep_target IN ITEMS SDL3::SDL3 SDL3_image::SDL3_image SDL3_mixer::SDL3_mixer)
        if(TARGET ${_dep_target})
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${_dep_target}>
                    $<TARGET_FILE_DIR:${target_name}>
                VERBATIM)
        endif()
    endforeach()
endfunction()
