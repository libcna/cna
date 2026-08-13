# --- Apple platform support (macOS + iOS) — plan_apple.md ---
#
# This file owns everything that is true for an Apple target *before* a renderer is chosen:
# which Apple platform the configure is targeting, the deployment-target/architecture defaults,
# the bundle metadata every iOS product is required to carry, and the renderer allow-list that
# keeps an impossible iOS configuration from reaching the compiler.
#
# It is included from the top-level CMakeLists.txt BEFORE cna_configure_vendored_sdl(), because
# the vendored SDL sub-builds are separate cmake invocations that must inherit the same
# sysroot/architecture/deployment target this file settles (see cmake/ThirdPartySDL.cmake).
#
# Support boundary (docs/apple-platforms.md): macOS has a native build/test gate. iOS is an
# experimental target: CI final-links device and simulator application bundles and launches one
# CNA frame in the simulator, but no physical-device, pixel, input, audio, storage or performance
# evidence exists. Nothing here claims complete iPhone support.

include_guard(GLOBAL)

# Keep paths owned by this module stable when CNA is consumed through add_subdirectory().
# CMAKE_SOURCE_DIR belongs to the outer application in that shape and must never be used to find
# CNA's plist templates or to decide which already-defined targets CNA's internal sweep owns.
get_filename_component(CNA_APPLE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CNA_APPLE_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

set(CNA_APPLE OFF)
set(CNA_APPLE_MACOS OFF)
set(CNA_APPLE_IOS OFF)
set(CNA_APPLE_IOS_SIMULATOR OFF)
set(CNA_APPLE_TARGET "NONE")

if(APPLE)
    set(CNA_APPLE ON)

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CNA_APPLE_MACOS ON)
        set(CNA_APPLE_TARGET "MACOS")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(CNA_APPLE_IOS ON)
        set(CNA_APPLE_TARGET "IOS")
        # CNA's runtime contains one tiny Objective-C++ adapter that asks UIKit to re-evaluate
        # supported orientations after XNA's GraphicsDeviceManager changes them at runtime.
        enable_language(OBJCXX)
    else()
        # tvOS/watchOS/visionOS share the Darwin toolchain but nothing in this project has ever
        # been configured, built or reasoned about for them. Fail loudly instead of letting a
        # macOS-shaped configuration silently pretend to target them.
        message(FATAL_ERROR
            "CNA: Apple target '${CMAKE_SYSTEM_NAME}' is not supported. CNA supports macOS "
            "(CMAKE_SYSTEM_NAME=Darwin) and iOS (CMAKE_SYSTEM_NAME=iOS, normally through "
            "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake).")
    endif()

    # The simulator and the device are the same CMAKE_SYSTEM_NAME/architecture pair on Apple
    # silicon (iOS + arm64) and are distinguishable only by sysroot. Everything downstream that
    # must not mix the two — most importantly the persistent SDL install root — keys off this.
    #
    # CNA_IOS_SIMULATOR (the toolchain file's own option) is the *request*; CNA_APPLE_IOS_SIMULATOR
    # below is the *observed truth*, derived from the sysroot actually in effect. They differ when
    # a caller passes -DCMAKE_OSX_SYSROOT directly, and the derived one is the one to trust.
    if(CNA_APPLE_IOS AND CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        set(CNA_APPLE_IOS_SIMULATOR ON)
        set(CNA_APPLE_TARGET "IOS_SIMULATOR")
    endif()
endif()

# ---------------------------------------------------------------------------
# Deployment targets
# ---------------------------------------------------------------------------
# CNA and sharp-runtime use floating-point std::to_chars unconditionally. Apple's libc++ marks
# those overloads unavailable before macOS 13.3 / iOS 16.3; suppressing the availability check
# would only defer the failure to the dynamic loader on an older device. These are therefore hard
# floors as well as defaults. Callers may raise them with CNA_*_DEPLOYMENT_TARGET or an explicit
# CMAKE_OSX_DEPLOYMENT_TARGET, but may not lower them.
set(CNA_MACOS_DEPLOYMENT_TARGET "13.3" CACHE STRING
    "Minimum macOS version CNA products are built for (CMAKE_OSX_DEPLOYMENT_TARGET default)")
set(CNA_IOS_DEPLOYMENT_TARGET "16.3" CACHE STRING
    "Minimum iOS version CNA products are built for (CMAKE_OSX_DEPLOYMENT_TARGET default)")

if(CNA_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    if(CNA_APPLE_IOS)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${CNA_IOS_DEPLOYMENT_TARGET}" CACHE STRING
            "Minimum Apple OS version" FORCE)
    else()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${CNA_MACOS_DEPLOYMENT_TARGET}" CACHE STRING
            "Minimum Apple OS version" FORCE)
    endif()
endif()

if(CNA_APPLE)
    if(CNA_APPLE_IOS)
        set(_cna_apple_minimum_deployment_target "16.3")
        set(_cna_apple_minimum_platform_name "iOS")
    else()
        set(_cna_apple_minimum_deployment_target "13.3")
        set(_cna_apple_minimum_platform_name "macOS")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS _cna_apple_minimum_deployment_target)
        message(FATAL_ERROR
            "CNA: ${_cna_apple_minimum_platform_name} deployment target "
            "${CMAKE_OSX_DEPLOYMENT_TARGET} is below the supported minimum "
            "${_cna_apple_minimum_deployment_target}. CNA/sharp-runtime use floating-point "
            "std::to_chars, which Apple libc++ does not provide on older releases.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Bundle identity
# ---------------------------------------------------------------------------
set(CNA_APPLE_BUNDLE_IDENTIFIER_PREFIX "com.openeggbert.cna" CACHE STRING
    "Reverse-DNS prefix for generated CFBundleIdentifier values (<prefix>.<target>)")
set(CNA_APPLE_BUNDLE_VERSION "1.0.0" CACHE STRING
    "CFBundleShortVersionString/CFBundleVersion written into generated Info.plist files")
set(CNA_APPLE_DEVELOPMENT_TEAM "" CACHE STRING
    "Apple Developer Team ID used to codesign iOS products; empty disables signing (simulator, CI)")
option(CNA_APPLE_BUNDLE_MACOS_EXECUTABLES
    "Emit macOS executables as .app bundles instead of plain command-line binaries" OFF)

# ---------------------------------------------------------------------------
# cna_apple_configure_bundle(<target>)
# ---------------------------------------------------------------------------
# Gives one executable target the Info.plist and signing configuration its Apple platform
# requires. On iOS this is mandatory: a bare Mach-O executable is not installable and SDL's
# UIKit entry point has nowhere to read the bundle metadata from. On macOS it is opt-in
# (CNA_APPLE_BUNDLE_MACOS_EXECUTABLES) because every example, tool and ctest binary in this
# repository is invoked by path, which a .app layout would break.
function(cna_apple_configure_bundle target)
    if(NOT CNA_APPLE)
        return()
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "cna_apple_configure_bundle: no such target '${target}'")
    endif()
    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "EXECUTABLE")
        return()
    endif()
    if(CNA_APPLE_MACOS AND NOT CNA_APPLE_BUNDLE_MACOS_EXECUTABLES)
        return()
    endif()
    get_target_property(_already_configured ${target} CNA_APPLE_BUNDLE_CONFIGURED)
    if(_already_configured)
        return()
    endif()
    set_property(TARGET ${target} PROPERTY CNA_APPLE_BUNDLE_CONFIGURED TRUE)

    # CFBundleIdentifier must be a valid reverse-DNS string: only alphanumerics, '-' and '.'.
    # CNA target names use underscores (cna_house3d_demo), which Apple rejects.
    string(REPLACE "_" "-" _bundle_suffix "${target}")

    if(CNA_APPLE_IOS)
        set(_plist "${CNA_APPLE_CMAKE_DIR}/AppleInfo.iOS.plist.in")
    else()
        set(_plist "${CNA_APPLE_CMAKE_DIR}/AppleInfo.macOS.plist.in")
    endif()

    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE                       TRUE
        MACOSX_BUNDLE_INFO_PLIST            "${_plist}"
        MACOSX_BUNDLE_BUNDLE_NAME           "${target}"
        MACOSX_BUNDLE_EXECUTABLE_NAME       "${target}"
        MACOSX_BUNDLE_GUI_IDENTIFIER        "${CNA_APPLE_BUNDLE_IDENTIFIER_PREFIX}.${_bundle_suffix}"
        MACOSX_BUNDLE_BUNDLE_VERSION        "${CNA_APPLE_BUNDLE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING  "${CNA_APPLE_BUNDLE_VERSION}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
            "${CNA_APPLE_BUNDLE_IDENTIFIER_PREFIX}.${_bundle_suffix}")

    if(CNA_APPLE_IOS)
        if(CNA_APPLE_DEVELOPMENT_TEAM)
            set_target_properties(${target} PROPERTIES
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${CNA_APPLE_DEVELOPMENT_TEAM}")
        else()
            # No team ID: the product is still a well-formed .app that runs on the simulator and
            # builds in CI, it simply cannot be installed on a device until it is signed.
            set_target_properties(${target} PROPERTIES
                XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
                XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED  "NO"
                XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY    "")
        endif()
    else()
        # A MACOSX_BUNDLE target does not automatically embed non-system dylibs. CNA's vendored
        # SDL and Homebrew FFmpeg would otherwise remain loadable only on the build machine via
        # its build RPATH. BundleUtilities copies the complete non-system dependency closure into
        # Contents/Frameworks and rewrites install names to @executable_path-relative paths.
        set(_bundle_search_dirs
            "${CNA_SDL_PREBUILT_ROOT}/install/lib"
            "/opt/homebrew/lib"
            "/usr/local/lib")
        string(REPLACE ";" "\\;" _bundle_search_dirs_arg "${_bundle_search_dirs}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DCNA_APP_BUNDLE=$<TARGET_BUNDLE_DIR:${target}>"
                "-DCNA_APP_LIBRARY_DIRS=${_bundle_search_dirs_arg}"
                -P "${CNA_APPLE_CMAKE_DIR}/FixupMacOSBundle.cmake"
            COMMENT "Embedding non-system runtime libraries in ${target}.app"
            VERBATIM)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# cna_apple_configure_all_bundles()
# ---------------------------------------------------------------------------
# CNA has ~200 executable targets spread over every module's examples/ directory, none of which
# knows anything about Apple. Rather than editing each one, the top-level CMakeLists sweeps the
# finished buildsystem once and hands every executable its bundle configuration.
function(cna_apple_configure_all_bundles)
    if(NOT CNA_APPLE)
        return()
    endif()
    if(CNA_APPLE_MACOS AND NOT CNA_APPLE_BUNDLE_MACOS_EXECUTABLES)
        return()
    endif()
    # Only sweep targets owned by CNA itself. A parent project commonly defines its application
    # after add_subdirectory(cna), so an automatic sweep can never reliably see downstream
    # targets; consumers call cna_apple_configure_bundle(their_target) explicitly instead.
    _cna_apple_configure_bundles_in_directory("${CNA_APPLE_SOURCE_DIR}")
endfunction()

function(_cna_apple_configure_bundles_in_directory dir)
    get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        cna_apple_configure_bundle(${_target})
    endforeach()

    get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_subdir IN LISTS _subdirs)
        _cna_apple_configure_bundles_in_directory("${_subdir}")
    endforeach()
endfunction()

# ---------------------------------------------------------------------------
# iOS renderer allow-list
# ---------------------------------------------------------------------------
# Called from cmake/RendererSelection.cmake once CNA_GRAPHICS_RENDERER is known. Renderers
# outside this list are not "probably broken" — they need a windowing/GL/native API that does not
# exist on iOS (desktop OpenGL, Direct3D, GDI, Glide, a browser DOM), or a third-party dependency
# this project has never configured for an iOS sysroot (Skia, Wicked, Diligent, bgfx, MoltenVK,
# wgpu-native, LLGL, sokol, Magnum, FNA3D). Configuring them would fail deep inside a dependency
# build with an unreadable error; this fails immediately with a readable one.
#
# Being on the list means CNA wires the renderer up for iOS, CI final-links it into a device app,
# and the simulator smoke app exercises one frame. It does not mean correct pixels or any input,
# audio, storage or physical-device behavior have been observed — see docs/apple-platforms.md.
set(CNA_APPLE_IOS_RENDERERS
    "SDL_RENDERER"  # SDL3's own 2D renderer; Metal-backed on iOS
    CACHE INTERNAL "Renderers CNA wires up for an iOS build" FORCE)

option(CNA_APPLE_ALLOW_UNVALIDATED_RENDERER
    "Allow selecting a renderer outside the iOS allow-list (experimentation only; expect build failures)"
    OFF)

function(cna_apple_validate_renderer renderer)
    if(NOT CNA_APPLE_IOS)
        return()
    endif()

    # list(FIND) rather than IN_LIST: this file is also parsed in cmake -P script mode by the
    # Apple smoke check, where policy CMP0057 (the one that turns IN_LIST into an operator) is
    # unset and the if() would be a hard error.
    list(FIND CNA_APPLE_IOS_RENDERERS "${renderer}" _renderer_index)
    if(NOT _renderer_index EQUAL -1)
        message(STATUS
            "CNA: iOS build using ${renderer} — experimental; CI final-links a device app and "
            "launches a simulator smoke app, with no physical-device or pixel evidence "
            "(docs/apple-platforms.md).")
        return()
    endif()

    if(CNA_APPLE_ALLOW_UNVALIDATED_RENDERER)
        message(WARNING
            "CNA: ${renderer} is outside the iOS allow-list (${CNA_APPLE_IOS_RENDERERS}) and is "
            "being configured only because CNA_APPLE_ALLOW_UNVALIDATED_RENDERER=ON. The build is "
            "expected to fail; nothing about this configuration is supported.")
        return()
    endif()

    message(FATAL_ERROR
        "CNA: ${renderer} cannot target iOS. CNA wires up these renderers for iOS: "
        "${CNA_APPLE_IOS_RENDERERS}. Pass -DCNA_APPLE_ALLOW_UNVALIDATED_RENDERER=ON to attempt "
        "another one anyway (unsupported, expect build failures) — see docs/apple-platforms.md.")
endfunction()

if(CNA_APPLE)
    message(STATUS
        "CNA: Apple target ${CNA_APPLE_TARGET} "
        "(deployment target ${CMAKE_OSX_DEPLOYMENT_TARGET}, sysroot '${CMAKE_OSX_SYSROOT}')")
endif()
