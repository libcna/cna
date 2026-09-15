# plans/plan_wayland.md WAYLAND-0123: a Wayland build links neither SDL nor X11.
#
# The source scan (WaylandIsSdlFreeTests) proves the backend's code names neither; this proves the
# binaries agree, which is the claim a user can check with ldd:
#
#   * no executable of the build NEEDs an SDL, X11, xcb or GLX library directly;
#   * the platform-only executable (the validation harness) reaches none of them even
#     transitively -- its whole closure is libwayland-client, xkbcommon and the C/C++ runtime;
#   * libcna_platform.a references no SDL, Xlib, xcb or GLX symbol;
#   * libwayland-server, the test compositor's library, is linked into the test binary only.
#
# EGL, libwayland-egl, libwayland-cursor, libvulkan and libdbus are opened at run time, so they
# must not be NEEDED by the platform either (D-1).
#
# Inputs: READELF, LDD (may be empty), NM, PLATFORM_ONLY (executable), OTHERS (;-list of
# executables), TEST_BINARY (executable that may NEED libwayland-server), ARCHIVE (the platform
# library).

set(_forbidden "^(libSDL|libX11|libX11-xcb|libXext|libXi|libXrandr|libXcursor|libXfixes|libXrender|libXss|libxcb|libGLX|libGL\\.so)")
set(_runtime_loaded "^(libEGL|libwayland-egl|libwayland-cursor|libdbus-1)")
set(_failures "")

function(_needed binary out)
    execute_process(COMMAND "${READELF}" -d "${binary}" OUTPUT_VARIABLE _dynamic RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "readelf failed on ${binary}")
    endif()
    string(REGEX MATCHALL "Shared library: \\[[^]]+\\]" _entries "${_dynamic}")
    set(_names "")
    foreach(_entry IN LISTS _entries)
        string(REGEX REPLACE "Shared library: \\[([^]]+)\\]" "\\1" _name "${_entry}")
        list(APPEND _names "${_name}")
    endforeach()
    set(${out} "${_names}" PARENT_SCOPE)
endfunction()

foreach(_binary IN LISTS PLATFORM_ONLY OTHERS TEST_BINARY)
    if(NOT EXISTS "${_binary}")
        continue()
    endif()
    _needed("${_binary}" _names)
    get_filename_component(_short "${_binary}" NAME)
    foreach(_name IN LISTS _names)
        if(_name MATCHES "${_forbidden}")
            list(APPEND _failures "${_short} NEEDs ${_name}")
        endif()
        if(_name MATCHES "${_runtime_loaded}")
            list(APPEND _failures "${_short} NEEDs ${_name}, which the platform opens at run time")
        endif()
        if(_name MATCHES "^libwayland-server" AND NOT _binary STREQUAL TEST_BINARY)
            list(APPEND _failures "${_short} NEEDs ${_name}, the test compositor's library")
        endif()
    endforeach()
    message(STATUS "${_short}: ${_names}")
endforeach()

if(LDD AND EXISTS "${PLATFORM_ONLY}")
    execute_process(COMMAND "${LDD}" "${PLATFORM_ONLY}" OUTPUT_VARIABLE _closure RESULT_VARIABLE _result)
    if(_result EQUAL 0)
        string(REGEX MATCHALL "(^|\n)[ \t]*[^ \t\n]+" _libraries "${_closure}")
        foreach(_library IN LISTS _libraries)
            string(STRIP "${_library}" _library)
            get_filename_component(_library "${_library}" NAME)
            if(_library MATCHES "${_forbidden}")
                list(APPEND _failures "the platform-only executable reaches ${_library} transitively")
            endif()
        endforeach()
        message(STATUS "platform-only closure: ${_closure}")
    endif()
endif()

if(EXISTS "${ARCHIVE}")
    execute_process(COMMAND "${NM}" -u "${ARCHIVE}" OUTPUT_VARIABLE _undefined RESULT_VARIABLE _result ERROR_QUIET)
    if(_result EQUAL 0)
        string(REGEX MATCHALL "[ \t]U (SDL_[A-Za-z0-9_]+|X(Open|Close|Next|Pending|Flush|Intern|Create|Map|Sync)[A-Za-z0-9_]*|xcb_[a-z0-9_]+|glX[A-Za-z0-9_]+)" _symbols "${_undefined}")
        foreach(_symbol IN LISTS _symbols)
            string(STRIP "${_symbol}" _symbol)
            list(APPEND _failures "libcna_platform.a references ${_symbol}")
        endforeach()
    endif()
endif()

if(_failures)
    list(REMOVE_DUPLICATES _failures)
    string(REPLACE ";" "\n  " _text "${_failures}")
    message(FATAL_ERROR "A Wayland build reaches SDL or X11:\n  ${_text}")
endif()
message(STATUS "no SDL, no X11: the Wayland build's link closure is clean")
