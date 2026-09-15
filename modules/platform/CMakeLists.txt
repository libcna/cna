# CNA platform module (plans/plan_platform.md PLAT-10): the CNA-owned contract that separates the
# framework from SDL3, plus the implementations of that contract.
#
# This module is the ONE place a platform library may be linked. Every other module talks to
# CNA::Platform's interfaces and never includes an SDL header -- enforced by the ratchet gate
# (cmake/PlatformRatchet.cmake, PLAT-8) and, from PLAT-121, as a hard error.
#
# src/ is flat for the contract's own types; each implementation owns a subdirectory
# (src/Sdl3/, src/Sdl2/, src/Win32/, src/X11/, src/Wayland/, src/Headless/, src/Terminal/), with
# src/Common/ holding the portable pieces more than one of them shares and src/Xkb/,
# src/Freedesktop/, src/Posix/ and src/Linux/ the ones only the Unix desktop backends share.
file(GLOB CNA_PLATFORM_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

# The selected implementation, chosen by CNA_PLATFORM (cmake/PlatformSelection.cmake).
if(CNA_PLATFORM STREQUAL "SDL3")
    file(GLOB CNA_PLATFORM_IMPL_SOURCES CONFIGURE_DEPENDS "src/Sdl3/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_IMPL_SOURCES})
    # The one Objective-C++ translation unit: asking UIKit to re-read the orientation set has no
    # C API. Listed explicitly rather than globbed, so it cannot be picked up on a target whose
    # compiler would reject it.
    if(CNA_APPLE_IOS)
        list(APPEND CNA_PLATFORM_SOURCES "src/Sdl3/Sdl3AppleOrientation.mm")
    endif()
endif()
if(CNA_PLATFORM STREQUAL "SDL2")
    file(GLOB CNA_PLATFORM_IMPL_SOURCES CONFIGURE_DEPENDS "src/Sdl2/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_IMPL_SOURCES})
endif()
# The native Win32 backend (plans/plan_win32.md). Unlike Headless and Terminal below it is NOT
# compiled unconditionally: every translation unit in src/Win32/ includes <windows.h>, so on a
# non-Windows target there is nothing to compile it against. cmake/PlatformSelection.cmake has
# already refused CNA_PLATFORM=WIN32 anywhere else, so this condition can never be the thing that
# silently drops a requested implementation.
if(CNA_PLATFORM STREQUAL "WIN32")
    file(GLOB CNA_PLATFORM_IMPL_SOURCES CONFIGURE_DEPENDS "src/Win32/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_IMPL_SOURCES})
endif()
# The native X11 backend (plans/plan_x11.md). Compiled only for its own selection: unlike
# Headless and Terminal, it has real third-party link inputs, and always compiling it would
# put libX11 into the link closure of every build on a Unix host -- exactly the kind of
# unconditional native dependency this backend exists to disprove.
if(CNA_PLATFORM STREQUAL "X11")
    file(GLOB CNA_PLATFORM_IMPL_SOURCES CONFIGURE_DEPENDS "src/X11/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_IMPL_SOURCES})
endif()
# The native Wayland backend (plans/plan_wayland.md), compiled only for its own selection for the
# same reason as X11: libwayland-client and xkbcommon must not reach the link closure of a build
# that did not ask for them. The protocol bindings are generated here by wayland-scanner from the
# installed wayland-protocols (cmake/PlatformWayland.cmake, D-2) -- nothing generated is committed.
if(CNA_PLATFORM STREQUAL "WAYLAND")
    file(GLOB CNA_PLATFORM_IMPL_SOURCES CONFIGURE_DEPENDS "src/Wayland/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_IMPL_SOURCES})
    cna_wayland_generate_protocols(CNA_WAYLAND_PROTOCOL_SOURCES CNA_WAYLAND_PROTOCOL_DIR)
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_WAYLAND_PROTOCOL_SOURCES})
    # For the test compositor, which includes the server-side headers generated beside these.
    set_property(GLOBAL PROPERTY CNA_WAYLAND_PROTOCOL_DIR "${CNA_WAYLAND_PROTOCOL_DIR}")
endif()

# What the native Unix desktop backends -- X11 and Wayland -- share (plans/plan_wayland.md Phase
# B). None of it is a window-system feature, so none of it lives under either backend:
#
#   src/Xkb/          XKB key names -> Scancode, keysyms -> KeyCode (no X or xkbcommon header)
#   src/Freedesktop/  libdbus loaded at run time, the desktop portal, the session bus's screen
#                     saver -- freedesktop.org services, on Linux and the BSDs alike
#   src/Posix/        the monotonic clock and the Vulkan loader lookup
#   src/Linux/        the kernel's evdev controllers and haptics, sysfs power and host facts
#
# Controllers come from the kernel, not from the window system: the X server has not delivered
# joystick input to clients for decades and Wayland never did. On Linux that is evdev, reached
# through nothing but the kernel's own <linux/input.h> -- no libudev, no libevdev. Another Unix
# (FreeBSD, say) has no such header and its build simply reports no gamepad and no joystick.
if(CNA_PLATFORM STREQUAL "X11" OR CNA_PLATFORM STREQUAL "WAYLAND")
    file(GLOB CNA_PLATFORM_UNIX_DESKTOP_SOURCES CONFIGURE_DEPENDS
        "src/Xkb/*.cpp" "src/Freedesktop/*.cpp" "src/Posix/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_UNIX_DESKTOP_SOURCES})

    include(CheckIncludeFileCXX)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        check_include_file_cxx("linux/input.h" CNA_PLATFORM_HAVE_LINUX_INPUT_H)
    endif()
    if(CNA_PLATFORM_HAVE_LINUX_INPUT_H)
        file(GLOB CNA_PLATFORM_LINUX_SOURCES CONFIGURE_DEPENDS "src/Linux/*.cpp")
        list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_LINUX_SOURCES})
        set(CNA_PLATFORM_HAVE_EVDEV TRUE)
        # For the test registration, which lives outside this directory's scope.
        set_property(GLOBAL PROPERTY CNA_PLATFORM_HAVE_EVDEV TRUE)
    endif()
endif()

# Implementation pieces shared between implementations that have no platform library to ask --
# std::filesystem path resolution, standard-library host facts. Not in the public include tree:
# an internal detail, not part of the contract.
file(GLOB CNA_PLATFORM_COMMON_SOURCES CONFIGURE_DEPENDS "src/Common/*.cpp")
list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_COMMON_SOURCES})

# Headless is ALWAYS compiled, whatever CNA_PLATFORM says. The conformance suite (PLAT-116) needs
# two implementations live in one process to be worth anything, and this one costs nothing to
# carry: it links no third-party library at all.
file(GLOB CNA_PLATFORM_HEADLESS_SOURCES CONFIGURE_DEPENDS "src/Headless/*.cpp")
list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_HEADLESS_SOURCES})

# Terminal is always compiled too, on every POSIX target, for the same reason and at the same
# cost -- termios and poll(2) are libc, not a third-party dependency. It is what makes PLAT-141's
# claim real: a THIRD implementation, one with a genuine display and genuine input that come from
# no windowing system at all, held to the same conformance suite. Windows has no termios, so the
# directory is excluded there rather than guarded file-by-file.
if(NOT WIN32)
    file(GLOB CNA_PLATFORM_TERMINAL_SOURCES CONFIGURE_DEPENDS "src/Terminal/*.cpp")
    list(APPEND CNA_PLATFORM_SOURCES ${CNA_PLATFORM_TERMINAL_SOURCES})
endif()
cna_add_module(cna_platform Platform ${CNA_PLATFORM_SOURCES})
cna_link_sharp_runtime(cna_platform PUBLIC Core.Base)

# Headers-only dependency on the core module for the CNAEXT marker macro, matching the math
# module's arrangement: no core symbol is referenced, so the link closure stays minimal.
target_link_libraries(cna_platform PUBLIC cna_core_headers)

# SDL3 is linked PRIVATE and only for the SDL3 selection: this module is the one place it may be
# linked at all, and even here it must not leak into the public include interface. The SDL-free
# probe (PLAT-27) is what proves it does not.
if(CNA_PLATFORM STREQUAL "SDL3")
    target_link_libraries(cna_platform PRIVATE SDL3::SDL3)

    # Android and iOS are the exception the probe cannot cover, because on exactly those two
    # targets CNA/Platform/Entrypoint.hpp really does include a platform header into the
    # consumer's own translation unit -- that is its whole job, and why PLAT-54 exempted it from
    # the SDL-free probe. A downstream executable therefore needs the include directory, and the
    # separately packaged main() implementation, or it compiles and then fails its final link on
    # a missing entry point. Desktop keeps the strict PRIVATE linkage above, so PLAT-27's probe
    # is unaffected: it builds for the host, never for a mobile target.
    if(ANDROID OR CNA_APPLE_IOS)
        target_link_libraries(cna_platform PUBLIC SDL3::SDL3)
        if(TARGET SDL3::SDL3main)
            target_link_libraries(cna_platform PUBLIC SDL3::SDL3main)
        endif()
    endif()

    if(CNA_APPLE_IOS)
        set_source_files_properties(src/Sdl3/Sdl3AppleOrientation.mm
                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_link_libraries(cna_platform PRIVATE "-framework UIKit")
    endif()
endif()
if(CNA_PLATFORM STREQUAL "SDL2")
    target_link_libraries(cna_platform PRIVATE SDL2::SDL2)
endif()

# The Win32 backend's native libraries, PRIVATE for the same reason SDL is: an implementation
# detail must not become a usage requirement of every consumer of CNA::Platform. None of these
# is a third-party dependency -- they are the operating system's own import libraries, always
# present in any Windows SDK or mingw-w64 sysroot, so there is nothing to fetch or vendor.
#
#   user32   windows, messages, input, clipboard, monitors, message boxes
#   gdi32    device contexts, StretchDIBits presentation, DIB cursors
#   opengl32 WGL context creation for IPlatformGlContext
#   ole32    CoInitializeEx / CoCreateInstance for the shell file dialogs
#   shell32  SHGetKnownFolderPath, ShellExecuteW
#   uuid     the CLSID/IID constants the shell dialog interfaces are named by
#
# Vulkan is deliberately absent: vulkan-1.dll is loaded at run time, so a machine with no Vulkan
# driver still runs a CNA binary -- the capability simply reports false.
if(CNA_PLATFORM STREQUAL "WIN32")
    target_link_libraries(cna_platform PRIVATE user32 gdi32 opengl32 ole32 shell32 uuid)
endif()
# Every X library is PRIVATE and so are the include roots: <X11/Xlib.h> must never reach a
# consumer's translation unit. `Display`, `Window`, `Atom` and `XEvent` appear in no public
# header -- the contract already carries an X11 window as an opaque `void* display` plus an
# integer XID, and that is the entire public surface of this backend. ContractIsSdlFreeTests
# and the module probes are what keep it that way.
if(CNA_PLATFORM STREQUAL "X11")
    target_link_libraries(cna_platform PRIVATE ${CNA_X11_LIBRARIES})
    if(CNA_X11_INCLUDE_DIRS)
        target_include_directories(cna_platform PRIVATE ${CNA_X11_INCLUDE_DIRS})
    endif()
    if(CNA_X11_DEFINITIONS)
        target_compile_definitions(cna_platform PRIVATE ${CNA_X11_DEFINITIONS})
    endif()
endif()
# Every Wayland library is PRIVATE, as X11's are: `wl_display` and `wl_surface` reach no public
# header -- the contract carries a Wayland window as two opaque pointers -- and libwayland-client
# and xkbcommon are the whole link closure this backend adds (EGL, libwayland-egl,
# libwayland-cursor, Vulkan and libdbus are opened at run time, D-1).
if(CNA_PLATFORM STREQUAL "WAYLAND")
    target_link_libraries(cna_platform PRIVATE ${CNA_WAYLAND_LIBRARIES})
    target_include_directories(cna_platform PRIVATE ${CNA_WAYLAND_INCLUDE_DIRS} ${CNA_WAYLAND_PROTOCOL_DIR})
    if(CNA_WAYLAND_DEFINITIONS)
        target_compile_definitions(cna_platform PRIVATE ${CNA_WAYLAND_DEFINITIONS})
    endif()
endif()
if(CNA_PLATFORM STREQUAL "X11" OR CNA_PLATFORM STREQUAL "WAYLAND")
    # dlopen/dlsym for the entry points both backends resolve at run time rather than linking
    # (GLX or EGL, Vulkan, libdbus); libdl is folded into glibc on modern Linux but is a separate
    # library on several other Unix hosts, so it is asked for rather than assumed.
    target_link_libraries(cna_platform PRIVATE ${CMAKE_DL_LIBS})
    if(CNA_PLATFORM_HAVE_EVDEV)
        target_compile_definitions(cna_platform PRIVATE CNA_PLATFORM_HAVE_EVDEV=1)
    endif()
endif()
