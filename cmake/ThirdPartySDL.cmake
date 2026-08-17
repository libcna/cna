include_guard(GLOBAL)

option(CNA_USE_SYSTEM_SDL "Use system SDL packages instead of vendored submodules" OFF)

# Keep configure-time vendored builds bounded as well as the top-level `cmake --build` invocation.
# A bare `cmake --build --parallel` defers to the build tool's unrestricted default and can exceed
# the requested CPU limit even when the caller passed `-j2` to the parent build.
set(CNA_MAX_VENDORED_BUILD_JOBS "2" CACHE STRING
    "Maximum parallel jobs for configure-time vendored dependency builds")
if(NOT CNA_MAX_VENDORED_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "CNA_MAX_VENDORED_BUILD_JOBS must be a positive integer; got '${CNA_MAX_VENDORED_BUILD_JOBS}'.")
endif()

# SDL is configured and installed into this directory at cmake-configure time.
# The directory lives OUTSIDE any cmake build tree, so cmake --build --clean-first
# (or deleting the build directory entirely) does NOT remove SDL artefacts.
# SDL is built once and reused across all builds and build types.
# To force a full SDL rebuild, delete this directory manually:
#   rm -rf <source_dir>/.sdl-prebuilt
if(EMSCRIPTEN)
    set(_cna_sdl_prebuilt_default "${CMAKE_CURRENT_SOURCE_DIR}/.sdl-prebuilt-emscripten")
else()
    # Keyed by target platform/arch so a cross-build (e.g. Windows via mingw-w64) cannot
    # silently overwrite the native build's cached SDL3 install, and vice versa.
    set(_cna_sdl_arch_key "${CMAKE_SYSTEM_PROCESSOR}")
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        # CMAKE_SYSTEM_PROCESSOR describes the host in a native macOS build, not an explicit
        # -DCMAKE_OSX_ARCHITECTURES override. Use the actual target architecture list so an
        # x86_64 or universal build on Apple silicon cannot reuse an arm64-only SDL install.
        set(_cna_sdl_arch_key "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    string(REPLACE ";" "-" _cna_sdl_arch_key "${_cna_sdl_arch_key}")
    set(_cna_sdl_key "${CMAKE_SYSTEM_NAME}-${_cna_sdl_arch_key}")
    # iOS device and iOS simulator are the same (system name, processor) pair on Apple silicon --
    # both "iOS-arm64" -- yet their binaries are not interchangeable (different platform in the
    # Mach-O build-version load command; linking one into the other is a hard error). The sysroot
    # is the only thing that distinguishes them, so it joins the key.
    if(CNA_APPLE_IOS_SIMULATOR)
        set(_cna_sdl_key "${_cna_sdl_key}-simulator")
    endif()
    if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
        # SDL object files carry their own minimum-OS load command. Reusing an install built for
        # a newer deployment target can produce a CNA binary that advertises an older floor but
        # still fails to load there, so the deployment target is part of the persistent key.
        string(REPLACE "." "_" _cna_sdl_deployment_key "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        set(_cna_sdl_key "${_cna_sdl_key}-min${_cna_sdl_deployment_key}")
    endif()
    set(_cna_sdl_prebuilt_default "${CMAKE_CURRENT_SOURCE_DIR}/.sdl-prebuilt-${_cna_sdl_key}")
endif()
set(CNA_SDL_PREBUILT_ROOT "${_cna_sdl_prebuilt_default}"
    CACHE PATH "Persistent SDL3 install root (survives cmake --clean and build-tree deletion)")

function(cna_configure_vendored_sdl)
    if(TARGET SDL3::SDL3 AND TARGET SDL3_image::SDL3_image AND TARGET SDL3_mixer::SDL3_mixer)
        return()
    endif()

    if(CNA_USE_SYSTEM_SDL)
        find_package(SDL3       REQUIRED)
        find_package(SDL3_image REQUIRED)
        find_package(SDL3_mixer REQUIRED)
        return()
    endif()

    set(_tp "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    foreach(_dep IN ITEMS SDL SDL_image SDL_mixer)
        if(NOT EXISTS "${_tp}/${_dep}/CMakeLists.txt")
            # Task DEV-BUILD-001: "git submodule update --init --recursive"
            # (the previous suggestion here) also recurses into SDL_image's/
            # SDL_mixer's own nested "external/*" codec submodules (AVIF,
            # JXL, WebP, libpng, GME, mod_xmp, mpg123, FluidSynth-MIDI, Opus,
            # Vorbis, ~19 total) -- none of which this project's own
            # SDLIMAGE_*/SDLMIXER_* CMAKE_ARGS below actually enable, and
            # cloning all of them measured 6-7x slower than the plain,
            # non-recursive form that is actually sufficient.
            message(FATAL_ERROR
                "Missing vendored '${_dep}' in ${_tp}. "
                "Run: git submodule update --init "
                "(non-recursive -- this project's CMAKE_ARGS below disable "
                "every optional codec dependency under SDL_image's/"
                "SDL_mixer's own nested submodules, so --recursive only "
                "adds a much slower, unnecessary fetch)")
        endif()
    endforeach()

    set(_prefix    "${CNA_SDL_PREBUILT_ROOT}/install")
    if(MSVC)
        # SDL projects intentionally install package configs directly under cmake/ for MSVC.
        set(_sdl3_cmake_dir       "${_prefix}/cmake")
        set(_sdl_image_cmake_dir  "${_prefix}/cmake")
        set(_sdl_mixer_cmake_dir  "${_prefix}/cmake")
    else()
        set(_cmake_dir            "${_prefix}/lib/cmake")
        set(_sdl3_cmake_dir       "${_cmake_dir}/SDL3")
        set(_sdl_image_cmake_dir  "${_cmake_dir}/SDL3_image")
        set(_sdl_mixer_cmake_dir  "${_cmake_dir}/SDL3_mixer")
    endif()

    # Platform-specific shared-library filenames
    if(EMSCRIPTEN)
        set(_sdl3_lib      "${_prefix}/lib/libSDL3.a")
        set(_sdl_image_lib "${_prefix}/lib/libSDL3_image.a")
        set(_sdl_mixer_lib "${_prefix}/lib/libSDL3_mixer.a")
        set(_sdl_shared OFF)
        set(_sdl_static  ON)
    elseif(ANDROID)
        set(_sdl3_lib      "${_prefix}/lib/libSDL3.so")
        set(_sdl_image_lib "${_prefix}/lib/libSDL3_image.so")
        set(_sdl_mixer_lib "${_prefix}/lib/libSDL3_mixer.so")
        set(_sdl_shared ON)
        set(_sdl_static OFF)
    elseif(WIN32)
        set(_sdl3_lib      "${_prefix}/bin/SDL3.dll")
        set(_sdl3_implib   "${_prefix}/lib/SDL3.lib")
        set(_sdl_image_lib "${_prefix}/bin/SDL3_image.dll")
        set(_sdl_image_implib "${_prefix}/lib/SDL3_image.lib")
        set(_sdl_mixer_lib "${_prefix}/bin/SDL3_mixer.dll")
        set(_sdl_mixer_implib "${_prefix}/lib/SDL3_mixer.lib")
        set(_sdl_shared ON)
        set(_sdl_static OFF)
    elseif(CNA_APPLE_IOS)
        # iOS: static only. A dylib inside an .app has to be embedded in Frameworks/, given an
        # @rpath install name and codesigned separately -- three things the plain
        # cmake --install used below does not do, and none of which the simulator forgives.
        # Linking SDL statically keeps the app a single signed Mach-O executable.
        set(_sdl3_lib      "${_prefix}/lib/libSDL3.a")
        set(_sdl_image_lib "${_prefix}/lib/libSDL3_image.a")
        set(_sdl_mixer_lib "${_prefix}/lib/libSDL3_mixer.a")
        set(_sdl_shared OFF)
        set(_sdl_static  ON)
    elseif(APPLE)
        set(_sdl3_lib      "${_prefix}/lib/libSDL3.dylib")
        set(_sdl_image_lib "${_prefix}/lib/libSDL3_image.dylib")
        set(_sdl_mixer_lib "${_prefix}/lib/libSDL3_mixer.dylib")
        set(_sdl_shared ON)
        set(_sdl_static OFF)
    else()
        # Linux / other Unix
        set(_sdl3_lib      "${_prefix}/lib/libSDL3.so")
        set(_sdl_image_lib "${_prefix}/lib/libSDL3_image.so")
        set(_sdl_mixer_lib "${_prefix}/lib/libSDL3_mixer.so")
        set(_sdl_shared ON)
        set(_sdl_static OFF)
    endif()

    # SDL is built using execute_process() at cmake-configure time.
    # This runs only when the library is absent (first configure, or after manual
    # deletion of CNA_SDL_PREBUILT_ROOT). cmake --build --clean-first never
    # re-runs cmake configure, so SDL is guaranteed to survive a clean build.
    if(NOT EXISTS "${_sdl3_lib}")
        _cna_build_sdl_dep(
            NAME     SDL3
            SOURCE   "${_tp}/SDL"
            BUILDDIR "${CNA_SDL_PREBUILT_ROOT}/SDL/build"
            CMAKE_ARGS
                -DSDL_SHARED=${_sdl_shared}
                -DSDL_STATIC=${_sdl_static}
                -DSDL_TESTS=OFF
                -DSDL_EXAMPLES=OFF
        )
    endif()

    if(NOT EXISTS "${_sdl_image_lib}")
        _cna_build_sdl_dep(
            NAME     SDL3_image
            SOURCE   "${_tp}/SDL_image"
            BUILDDIR "${CNA_SDL_PREBUILT_ROOT}/SDL_image/build"
            CMAKE_ARGS
                "-DCMAKE_PREFIX_PATH=${_prefix}"
                "-DSDL3_DIR=${_sdl3_cmake_dir}"
                -DBUILD_SHARED_LIBS=${_sdl_shared}
                -DSDLIMAGE_DEPS_SHARED=${_sdl_shared}
                -DSDLIMAGE_INSTALL=ON
                -DSDLIMAGE_VENDORED=ON
                -DSDLIMAGE_TESTS=OFF
                -DSDLIMAGE_SAMPLES=OFF
                -DSDLIMAGE_AVIF=OFF
                -DSDLIMAGE_JXL=OFF
                -DSDLIMAGE_TIF=OFF
                -DSDLIMAGE_WEBP=OFF
                -DSDLIMAGE_PNG_LIBPNG=OFF
        )
    endif()

    if(NOT EXISTS "${_sdl_mixer_lib}")
        _cna_build_sdl_dep(
            NAME     SDL3_mixer
            SOURCE   "${_tp}/SDL_mixer"
            BUILDDIR "${CNA_SDL_PREBUILT_ROOT}/SDL_mixer/build"
            CMAKE_ARGS
                "-DCMAKE_PREFIX_PATH=${_prefix}"
                "-DSDL3_DIR=${_sdl3_cmake_dir}"
                -DBUILD_SHARED_LIBS=${_sdl_shared}
                -DSDLMIXER_DEPS_SHARED=${_sdl_shared}
                -DSDLMIXER_INSTALL=ON
                -DSDLMIXER_VENDORED=ON
                -DSDLMIXER_TESTS=OFF
                -DSDLMIXER_EXAMPLES=OFF
                -DSDLMIXER_GME=OFF
                -DSDLMIXER_MOD_XMP=OFF
                -DSDLMIXER_MP3_MPG123=OFF
                -DSDLMIXER_MIDI_FLUIDSYNTH=OFF
                -DSDLMIXER_OPUS=OFF
                -DSDLMIXER_VORBIS_VORBISFILE=OFF
                -DSDLMIXER_VORBIS_TREMOR=OFF
                -DSDLMIXER_WAVPACK=OFF
                -DSDLMIXER_FLAC_LIBFLAC=OFF
        )
    endif()

    # SDL is now installed — let find_package set up the targets properly.
    set(SDL3_DIR       "${_sdl3_cmake_dir}"      CACHE PATH "" FORCE)
    set(SDL3_image_DIR "${_sdl_image_cmake_dir}" CACHE PATH "" FORCE)
    set(SDL3_mixer_DIR "${_sdl_mixer_cmake_dir}" CACHE PATH "" FORCE)
    find_package(SDL3       REQUIRED CONFIG)
    find_package(SDL3_image REQUIRED CONFIG)
    find_package(SDL3_mixer REQUIRED CONFIG)

    # Windows resolves a DLL from the executable's own directory first and has no RPATH, so an
    # executable linked against these import libraries cannot even start unless the DLLs sit
    # beside it -- the loader fails the process before main() with 0xC0000135, which looks
    # nothing like a test failure. Every test executable is written to CMAKE_BINARY_DIR, so one
    # copy there serves all of them. This runs at configure time because that is when the SDL
    # build above produced the DLLs; a POST_BUILD step per target would copy the same files
    # dozens of times for no benefit.
    if(WIN32)
        file(GLOB _cna_sdl_runtime_dlls "${_prefix}/bin/*.dll")
        if(_cna_sdl_runtime_dlls)
            file(COPY ${_cna_sdl_runtime_dlls} DESTINATION "${CMAKE_BINARY_DIR}")
            list(LENGTH _cna_sdl_runtime_dlls _cna_sdl_dll_count)
            message(STATUS "CNA: copied ${_cna_sdl_dll_count} SDL runtime DLL(s) next to the test executables")
        else()
            message(WARNING
                "CNA: no SDL runtime DLLs found in ${_prefix}/bin -- Windows test executables "
                "will fail to start if they link SDL.")
        endif()
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Internal helper: configure + build + install one SDL dependency.
# ---------------------------------------------------------------------------
function(_cna_build_sdl_dep)
    cmake_parse_arguments(_A "" "NAME;SOURCE;BUILDDIR" "CMAKE_ARGS" ${ARGN})

    set(_prefix "${CNA_SDL_PREBUILT_ROOT}/install")

    set(_base_args
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_INSTALL_PREFIX=${_prefix}"
    )
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _base_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(ANDROID)
        # The NDK's own toolchain file determines the target ABI/platform from these cache
        # variables, not from CMAKE_TOOLCHAIN_FILE alone - each SDL sub-build is a fully separate
        # cmake invocation (execute_process below), so it doesn't inherit the parent configure's
        # cache automatically. Without this, every sub-build silently falls back to the NDK
        # toolchain's own defaults (historically ARM32 / minimum supported platform).
        list(APPEND _base_args "-DANDROID_ABI=${ANDROID_ABI}")
        if(ANDROID_PLATFORM)
            list(APPEND _base_args "-DANDROID_PLATFORM=${ANDROID_PLATFORM}")
        endif()
        if(ANDROID_STL)
            list(APPEND _base_args "-DANDROID_STL=${ANDROID_STL}")
        endif()
    endif()
    if(APPLE)
        # Same reasoning as the ANDROID block above: each SDL sub-build is a separate cmake
        # invocation and inherits none of this configure's cache. Without these, an iOS configure
        # silently builds SDL for the *host* macOS (the toolchain file sets CMAKE_SYSTEM_NAME but
        # not the sysroot/architecture cache entries the sub-build needs), and the resulting
        # archive is rejected at link time as "built for macOS" -- or, worse on an Apple silicon
        # host targeting the simulator, links and then misbehaves. CMAKE_OSX_DEPLOYMENT_TARGET
        # also has to match, or SDL's objects carry a different minimum-version load command than
        # CNA's.
        foreach(_apple_var IN ITEMS
                CMAKE_OSX_SYSROOT CMAKE_OSX_ARCHITECTURES CMAKE_OSX_DEPLOYMENT_TARGET)
            # if(${_apple_var}) would test the *value* as a condition -- "iphoneos" is not a
            # defined variable, so it would evaluate false and silently drop the sysroot.
            set(_apple_value "${${_apple_var}}")
            if(NOT _apple_value STREQUAL "")
                list(APPEND _base_args "-D${_apple_var}=${_apple_value}")
            endif()
        endforeach()
        if(CNA_APPLE_IOS)
            list(APPEND _base_args "-DCMAKE_SYSTEM_NAME=iOS")
            list(APPEND _base_args "-DCNA_IOS_SIMULATOR=${CNA_IOS_SIMULATOR}")
            # SDL3_image/SDL3_mixer resolve SDL3 through find_package(SDL3 CONFIG); the iOS
            # toolchain's re-rooted find rules would otherwise reject the install prefix.
            list(APPEND _base_args "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH")
        endif()
    endif()

    message(STATUS "CNA: Configuring ${_A_NAME} (one-time step)...")
    execute_process(
        COMMAND ${CMAKE_COMMAND}
            ${_base_args}
            ${_A_CMAKE_ARGS}
            -S "${_A_SOURCE}"
            -B "${_A_BUILDDIR}"
        RESULT_VARIABLE _rc
    )
    if(_rc)
        message(FATAL_ERROR "CNA: ${_A_NAME} cmake configure failed (exit code ${_rc})")
    endif()

    message(STATUS "CNA: Building ${_A_NAME} (one-time step)...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${_A_BUILDDIR}"
            --parallel "${CNA_MAX_VENDORED_BUILD_JOBS}"
        RESULT_VARIABLE _rc
    )
    if(_rc)
        message(FATAL_ERROR "CNA: ${_A_NAME} build failed (exit code ${_rc})")
    endif()

    message(STATUS "CNA: Installing ${_A_NAME}...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --install "${_A_BUILDDIR}"
        RESULT_VARIABLE _rc
    )
    if(_rc)
        message(FATAL_ERROR "CNA: ${_A_NAME} install failed (exit code ${_rc})")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Copy the MinGW threading runtime DLL (libwinpthread-1.dll) next to a target
# executable on Windows/MinGW builds.
# ---------------------------------------------------------------------------
function(cna_copy_mingw_runtime target_name)
    if(NOT MINGW)
        return()
    endif()
    if(EMSCRIPTEN OR ANDROID)
        return()
    endif()

    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -print-file-name=libwinpthread-1.dll
        OUTPUT_VARIABLE _pthread_dll
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(_pthread_dll AND _pthread_dll MATCHES "[/\\\\]")
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

# Copy the GCC and C++ runtime DLLs for a MinGW executable that intentionally
# uses the dynamic C++ runtime.  This is separate from cna_copy_mingw_runtime:
# test targets can still statically link libgcc/libstdc++ and only need the
# threading DLL, while application targets with large RTTI graphs may not be
# compatible with -static-libstdc++.
function(cna_copy_mingw_cxx_runtime target_name)
    if(NOT MINGW OR EMSCRIPTEN OR ANDROID)
        return()
    endif()

    foreach(_runtime_dll IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" "-print-file-name=${_runtime_dll}"
            OUTPUT_VARIABLE _runtime_dll_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_runtime_dll_path AND _runtime_dll_path MATCHES "[/\\\\]" AND EXISTS "${_runtime_dll_path}")
            file(TO_CMAKE_PATH "${_runtime_dll_path}" _runtime_dll_path)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_runtime_dll_path}"
                    $<TARGET_FILE_DIR:${target_name}>
                COMMENT "Copying ${_runtime_dll} next to ${target_name}"
                VERBATIM)
        else()
            message(WARNING
                "cna_copy_mingw_cxx_runtime: could not locate ${_runtime_dll}. "
                "The executable may fail to run on machines without MinGW installed.")
        endif()
    endforeach()

    cna_copy_mingw_runtime(${target_name})
endfunction()

# ---------------------------------------------------------------------------
# Copy SDL shared libraries next to the target executable on Windows.
# ---------------------------------------------------------------------------
function(cna_copy_sdl_runtime target_name)
    if(EMSCRIPTEN OR ANDROID OR NOT WIN32)
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
