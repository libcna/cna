# SPDX-License-Identifier: MS-PL
#
# plans/plan_win32_native_validation.md WINNATIVE-0030: mark a target as a Windows GUI application
# without breaking the entry point CNA actually uses.
#
# `WIN32_EXECUTABLE TRUE` links with the GUI subsystem, which is what stops a window from opening
# a console behind it. The subsystem also decides which entry point the C runtime looks for, and
# the two toolchains do not agree about what happens next:
#
#   * MinGW's CRT supplies a `WinMain` that calls the program's `main()`, so a GUI-subsystem
#     executable whose entry point is `main()` links and runs;
#   * MSVC's CRT does not. It looks for `WinMain` and nothing else, and a target that does not
#     define one fails to link with
#         MSVCRT.lib(exe_winmain.obj) : error LNK2019: unresolved external symbol WinMain
#
# CNA applications define `main()`. That is a deliberate, documented decision rather than an
# accident -- see `modules/platform/include/CNA/Platform/Entrypoint.hpp`, which states that the
# Win32 backend takes nothing over from the entry point, because doing so "would be a cost with no
# benefit, and would break a console or test host that has its own". So the fix belongs in the
# build, not in the sources: naming the standard console entry point keeps `main()` **and** keeps
# the GUI subsystem, which is the combination the property was asked for in the first place.
#
# Until this existed, every one of CNA's example applications -- 27 targets across eight modules --
# was unlinkable with the Microsoft toolchain. It went unnoticed because the Windows executables
# this project had produced were all cross-compiled with MinGW, where the CRT covers for it.

# cna_windows_gui_executable(<target>)
#
# Marks <target> as a Windows GUI application. A no-op away from Windows, so call sites do not need
# their own `if(WIN32)`.
function(cna_windows_gui_executable target)
    if(NOT WIN32)
        return()
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "cna_windows_gui_executable: no such target: ${target}")
    endif()
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MSVC)
        target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
    endif()
endfunction()
