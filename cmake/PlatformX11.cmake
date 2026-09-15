# =====================================================================================
# CNA native X11 platform dependencies (plans/plan_x11.md Task X11-0002)
#
# Discovers the X libraries the X11 backend uses, one at a time. Only `X11` and `Xext`
# are mandatory; every other extension is genuinely optional and turns off exactly one
# capability when it is absent (design decision 14). That granularity is the point:
# requiring Xrandr for a backend that can open a window without it would make the
# backend unavailable on systems where it would work perfectly.
#
# This file defines nothing globally. It sets, in the caller's scope:
#
#   CNA_X11_AVAILABLE            TRUE when the mandatory set is present
#   CNA_X11_UNAVAILABLE_REASON   why not, when it is not
#   CNA_X11_LIBRARIES            the link closure for cna_platform (PRIVATE)
#   CNA_X11_INCLUDE_DIRS         include roots for cna_platform (PRIVATE)
#   CNA_X11_DEFINITIONS          CNA_X11_HAVE_<EXT> for each optional piece found (D-Bus too)
#   CNA_X11_SUMMARY              one human-readable line for the configure log
#
# No distro-specific include or library path is hardcoded anywhere: the search is
# find_package(X11) (CMake's own module, which itself consults pkg-config on Unix)
# with a pkg-config fallback for the pieces older FindX11 modules do not report.
# =====================================================================================

include_guard(GLOBAL)

function(cna_detect_x11)
    set(CNA_X11_AVAILABLE FALSE PARENT_SCOPE)
    set(CNA_X11_UNAVAILABLE_REASON "" PARENT_SCOPE)
    set(CNA_X11_LIBRARIES "" PARENT_SCOPE)
    set(CNA_X11_INCLUDE_DIRS "" PARENT_SCOPE)
    set(CNA_X11_DEFINITIONS "" PARENT_SCOPE)
    set(CNA_X11_SUMMARY "" PARENT_SCOPE)

    # X11 is a windowing system for Unix-like hosts. A Windows or Emscripten target has no
    # X server to talk to, and offering the selection there would produce a configure that
    # succeeds and a build that does not -- the same reasoning that keeps TERMINAL off Windows.
    if(WIN32 OR EMSCRIPTEN OR ANDROID OR CNA_APPLE_IOS)
        set(CNA_X11_UNAVAILABLE_REASON
            "X11 is a Unix windowing system; this target has no X server" PARENT_SCOPE)
        return()
    endif()

    find_package(X11 QUIET)
    if(NOT X11_FOUND OR NOT X11_X11_LIB)
        set(CNA_X11_UNAVAILABLE_REASON
            "libX11 development files were not found (Debian/Ubuntu: libx11-dev; \
Fedora: libX11-devel; FreeBSD: x11/libX11)" PARENT_SCOPE)
        return()
    endif()

    # Xext carries MIT-SHM's client library and is present wherever libX11 is. It is treated as
    # mandatory rather than optional because every practical X11 installation ships the pair and
    # splitting them would add a build permutation nobody can exercise.
    if(NOT X11_Xext_LIB)
        set(CNA_X11_UNAVAILABLE_REASON
            "libXext development files were not found (Debian/Ubuntu: libxext-dev)" PARENT_SCOPE)
        return()
    endif()

    # XKB lives inside libX11 itself, so there is no separate library to find -- only the header,
    # which older/stripped installations can omit. Without it the backend cannot derive a
    # layout-independent scancode at all (design decision 3), so it is part of the mandatory set.
    if(NOT X11_Xkb_INCLUDE_PATH AND NOT EXISTS "${X11_X11_INCLUDE_PATH}/X11/XKBlib.h")
        set(CNA_X11_UNAVAILABLE_REASON
            "X11/XKBlib.h was not found; the X11 backend derives layout-independent scancodes \
from XKB key names and has no equivalent without it" PARENT_SCOPE)
        return()
    endif()

    set(_libraries ${X11_X11_LIB} ${X11_Xext_LIB})
    set(_includes ${X11_X11_INCLUDE_PATH})
    set(_definitions "")
    set(_found_optional "")
    set(_missing_optional "")

    # --- optional extensions, each gating exactly one capability ------------------------------
    #
    # Xi       -> relativeMouse, inputDeviceEnumeration  (XInput2 raw motion, device list)
    # Xrandr   -> multipleDisplays                       (monitor enumeration and hotplug)
    # Xcursor  -> custom cursor images                   (core X11 still gives shaped cursors)
    # Xfixes   -> pointer hiding without an owned pixmap
    # Xau      -> the exclusive-fullscreen mode guardian's own connection to a server that asks
    #             for an authorisation cookie (plans/plan_x11.md X11-0153); libX11 already
    #             depends on it, so it is present wherever libX11 is
    # Xss      -> a screen saver suspended for this client alone, and given back if it dies
    #             (MIT-SCREEN-SAVER 1.1, plans/plan_x11.md X11-0170)
    foreach(_ext Xi Xrandr Xcursor Xfixes Xau Xss)
        string(TOUPPER "${_ext}" _ext_upper)
        if(X11_${_ext}_FOUND AND X11_${_ext}_LIB)
            list(APPEND _libraries ${X11_${_ext}_LIB})
            if(X11_${_ext}_INCLUDE_PATH)
                list(APPEND _includes ${X11_${_ext}_INCLUDE_PATH})
            endif()
            list(APPEND _definitions "CNA_X11_HAVE_${_ext_upper}=1")
            list(APPEND _found_optional "${_ext}")
        else()
            list(APPEND _missing_optional "${_ext}")
        endif()
    endforeach()

    # MIT-SHM is an optimisation for the software presenter, never a requirement: the presenter
    # falls back to a plain XPutImage, which works over a network connection where shared memory
    # cannot. The client side lives in Xext, which is already linked, so only the header matters.
    if(X11_XShm_FOUND OR EXISTS "${X11_X11_INCLUDE_PATH}/X11/extensions/XShm.h")
        list(APPEND _definitions "CNA_X11_HAVE_XSHM=1")
        list(APPEND _found_optional "XShm")
    else()
        list(APPEND _missing_optional "XShm")
    endif()

    # GLX backs IPlatformGlContext. Asked for through FindOpenGL's GLX component rather than
    # FindX11, because the GLX client library is part of the GL implementation (libglvnd or Mesa),
    # not of the X client libraries.
    #
    # HEADERS ONLY, like Vulkan below: the backend resolves its GLX entry points from libGLX (or
    # libGL) at run time. Linking it made every X11 build -- HEADLESS and SOFTWARE included --
    # need a GL implementation installed to start, and put a GL library into the link closure of
    # every module above the platform (plans/plan_native_platform_validation.md NPV-0121).
    find_package(OpenGL QUIET COMPONENTS OpenGL GLX)
    if(TARGET OpenGL::GLX)
        get_target_property(_glx_includes OpenGL::GLX INTERFACE_INCLUDE_DIRECTORIES)
        if(_glx_includes)
            list(APPEND _includes ${_glx_includes})
        endif()
        list(APPEND _definitions "CNA_X11_HAVE_GLX=1")
        list(APPEND _found_optional "GLX")
    else()
        list(APPEND _missing_optional "GLX")
    endif()

    # Vulkan: HEADERS ONLY, deliberately. The surface service resolves vkCreateXlibSurfaceKHR
    # through the vkGetInstanceProcAddr the caller already has, so the platform links no Vulkan
    # loader and a machine with no Vulkan driver still builds and runs everything else.
    find_package(Vulkan QUIET)
    if(Vulkan_INCLUDE_DIRS)
        list(APPEND _includes ${Vulkan_INCLUDE_DIRS})
        list(APPEND _definitions "CNA_X11_HAVE_VULKAN_HEADERS=1")
        list(APPEND _found_optional "Vulkan headers")
    else()
        list(APPEND _missing_optional "Vulkan headers")
    endif()

    # D-Bus: HEADERS ONLY, like Vulkan. The desktop portal -- file dialogs and OpenUrl
    # (plans/plan_x11.md X11-0169) -- is reached through libdbus loaded at run time, so a machine
    # without it (a container, a bare X server) builds and runs everything else, and simply has
    # no portal to offer.
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(_cna_dbus QUIET dbus-1)
    endif()
    if(_cna_dbus_FOUND)
        list(APPEND _includes ${_cna_dbus_INCLUDE_DIRS})
        list(APPEND _definitions "CNA_X11_HAVE_DBUS=1")
        list(APPEND _found_optional "D-Bus headers")
    else()
        list(APPEND _missing_optional "D-Bus headers")
    endif()

    list(REMOVE_DUPLICATES _includes)
    list(JOIN _found_optional ", " _found_text)
    list(JOIN _missing_optional ", " _missing_text)
    if(_missing_optional)
        set(_summary "X11 + Xext; optional present: ${_found_text}; absent: ${_missing_text}")
    else()
        set(_summary "X11 + Xext; optional present: ${_found_text}")
    endif()

    set(CNA_X11_AVAILABLE TRUE PARENT_SCOPE)
    set(CNA_X11_LIBRARIES "${_libraries}" PARENT_SCOPE)
    set(CNA_X11_INCLUDE_DIRS "${_includes}" PARENT_SCOPE)
    set(CNA_X11_DEFINITIONS "${_definitions}" PARENT_SCOPE)
    set(CNA_X11_SUMMARY "${_summary}" PARENT_SCOPE)
endfunction()
