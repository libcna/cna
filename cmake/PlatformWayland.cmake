# =====================================================================================
# CNA native Wayland platform dependencies (plans/plan_wayland.md WAYLAND-0020/0021)
#
# Discovers what the Wayland backend is built from, piece by piece. The mandatory set is small
# and each member is the backend's reason to exist:
#
#   wayland-client    the connection, the registry, every proxy
#   xkbcommon         the keyboard -- a Wayland compositor sends a keymap, nothing more
#   wayland-scanner   turns protocol XML into the bindings, at build time
#   wayland-protocols the XML of xdg-shell (mandatory) and the optional protocols
#
# Everything else is optional and turns off exactly one capability when absent (D-1, D-2):
#
#   egl + wayland-egl HEADERS  -> openGlContext   (both libraries loaded at run time)
#   wayland-cursor HEADERS     -> the cursor-theme fallback (loaded at run time)
#   Vulkan HEADERS             -> vulkanSurface   (the loader is the caller's)
#   dbus-1 HEADERS             -> the desktop portal and the session-bus screen saver
#   each optional protocol XML -> the capability it carries (CNA_WAYLAND_HAVE_<PROTOCOL>)
#
# This file defines nothing globally. cna_detect_wayland() sets, in the caller's scope:
#
#   CNA_WAYLAND_AVAILABLE            TRUE when the mandatory set is present
#   CNA_WAYLAND_UNAVAILABLE_REASON   why not, naming the packages to install
#   CNA_WAYLAND_LIBRARIES            the link closure of cna_platform (PRIVATE)
#   CNA_WAYLAND_INCLUDE_DIRS         include roots for cna_platform (PRIVATE)
#   CNA_WAYLAND_DEFINITIONS          CNA_WAYLAND_HAVE_* and CNA_PLATFORM_HAVE_DBUS
#   CNA_WAYLAND_SERVER_LIBRARIES     libwayland-server, for the test compositor only
#   CNA_WAYLAND_SCANNER              the wayland-scanner executable
#   CNA_WAYLAND_PROTOCOLS            name;xml pairs of every protocol to generate
#   CNA_WAYLAND_SUMMARY              one human-readable line for the configure log
#
# No distro path is hardcoded: pkg-config answers every question, including where
# wayland-protocols keeps its XML and which wayland-scanner belongs to the libraries found.
# =====================================================================================

include_guard(GLOBAL)

function(cna_detect_wayland)
    set(CNA_WAYLAND_AVAILABLE FALSE PARENT_SCOPE)
    set(CNA_WAYLAND_UNAVAILABLE_REASON "" PARENT_SCOPE)
    set(CNA_WAYLAND_LIBRARIES "" PARENT_SCOPE)
    set(CNA_WAYLAND_INCLUDE_DIRS "" PARENT_SCOPE)
    set(CNA_WAYLAND_DEFINITIONS "" PARENT_SCOPE)
    set(CNA_WAYLAND_SERVER_LIBRARIES "" PARENT_SCOPE)
    set(CNA_WAYLAND_SCANNER "" PARENT_SCOPE)
    set(CNA_WAYLAND_PROTOCOLS "" PARENT_SCOPE)
    set(CNA_WAYLAND_SUMMARY "" PARENT_SCOPE)

    # Wayland is a Unix display protocol. Windows, the browser, Android and Apple's platforms
    # have no Wayland compositor to connect to; offering the selection there would produce a
    # configure that succeeds and a build that does not.
    if(WIN32 OR EMSCRIPTEN OR ANDROID OR APPLE OR CNA_APPLE_IOS)
        set(CNA_WAYLAND_UNAVAILABLE_REASON
            "Wayland is a Unix display protocol; this target has no Wayland compositor" PARENT_SCOPE)
        return()
    endif()

    find_package(PkgConfig QUIET)
    if(NOT PKG_CONFIG_FOUND)
        set(CNA_WAYLAND_UNAVAILABLE_REASON
            "pkg-config was not found; the Wayland libraries and wayland-protocols are located \
through it (Debian/Ubuntu: pkg-config)" PARENT_SCOPE)
        return()
    endif()

    # 1.18 (2020): the oldest libwayland whose client API has everything the backend calls --
    # wl_proxy_create_wrapper, wl_proxy_get_version, the prepare_read family -- with the bugs
    # of the first releases of those fixed.
    pkg_check_modules(_cna_wl_client QUIET wayland-client>=1.18)
    if(NOT _cna_wl_client_FOUND)
        set(CNA_WAYLAND_UNAVAILABLE_REASON
            "wayland-client >= 1.18 development files were not found (Debian/Ubuntu: \
libwayland-dev; Fedora: wayland-devel; Arch: wayland)" PARENT_SCOPE)
        return()
    endif()

    # 0.5: the first xkbcommon with the compose API (xkb_compose_*), which dead keys need.
    pkg_check_modules(_cna_xkb QUIET xkbcommon>=0.5)
    if(NOT _cna_xkb_FOUND)
        set(CNA_WAYLAND_UNAVAILABLE_REASON
            "xkbcommon >= 0.5 development files were not found (Debian/Ubuntu: \
libxkbcommon-dev; Fedora: libxkbcommon-devel)" PARENT_SCOPE)
        return()
    endif()

    pkg_check_modules(_cna_wl_protocols QUIET wayland-protocols)
    if(NOT _cna_wl_protocols_FOUND)
        set(CNA_WAYLAND_UNAVAILABLE_REASON
            "wayland-protocols was not found (Debian/Ubuntu: wayland-protocols; Fedora: \
wayland-protocols-devel)" PARENT_SCOPE)
        return()
    endif()
    pkg_get_variable(_protocols_dir wayland-protocols pkgdatadir)

    # The scanner named by the wayland-scanner package is the one that matches the libraries;
    # PATH is only the fallback for a distribution that does not ship its .pc file.
    pkg_check_modules(_cna_wl_scanner QUIET wayland-scanner)
    set(_scanner "")
    if(_cna_wl_scanner_FOUND)
        pkg_get_variable(_scanner wayland-scanner wayland_scanner)
    endif()
    if(NOT _scanner OR NOT EXISTS "${_scanner}")
        find_program(CNA_WAYLAND_SCANNER_PROGRAM wayland-scanner)
        set(_scanner "${CNA_WAYLAND_SCANNER_PROGRAM}")
    endif()
    if(NOT _scanner)
        set(CNA_WAYLAND_UNAVAILABLE_REASON
            "wayland-scanner was not found (Debian/Ubuntu: libwayland-bin; Fedora: \
wayland-devel)" PARENT_SCOPE)
        return()
    endif()

    # xdg-shell is the one protocol the backend cannot do without: it is how a surface becomes a
    # window. Stable since wayland-protocols 1.12.
    set(_xdg_shell "${_protocols_dir}/stable/xdg-shell/xdg-shell.xml")
    if(NOT EXISTS "${_xdg_shell}")
        set(CNA_WAYLAND_UNAVAILABLE_REASON
            "wayland-protocols at ${_protocols_dir} has no stable/xdg-shell/xdg-shell.xml \
(wayland-protocols >= 1.12 is required)" PARENT_SCOPE)
        return()
    endif()

    set(_libraries ${_cna_wl_client_LINK_LIBRARIES} ${_cna_xkb_LINK_LIBRARIES})
    set(_includes ${_cna_wl_client_INCLUDE_DIRS} ${_cna_xkb_INCLUDE_DIRS})
    set(_definitions "")
    set(_found_optional "")
    set(_missing_optional "")

    # --- optional protocols ---------------------------------------------------------------------
    #
    # name | path under wayland-protocols | definition. Each is generated only when its XML
    # exists, and each turns off its own capability when it does not; older wayland-protocols
    # releases simply lack the staging ones.
    set(_protocols "xdg-shell;${_xdg_shell}")
    set(_optional
        "xdg-output-unstable-v1|unstable/xdg-output/xdg-output-unstable-v1.xml|XDG_OUTPUT"
        "viewporter|stable/viewporter/viewporter.xml|VIEWPORTER"
        "fractional-scale-v1|staging/fractional-scale/fractional-scale-v1.xml|FRACTIONAL_SCALE"
        "relative-pointer-unstable-v1|unstable/relative-pointer/relative-pointer-unstable-v1.xml|RELATIVE_POINTER"
        "pointer-constraints-unstable-v1|unstable/pointer-constraints/pointer-constraints-unstable-v1.xml|POINTER_CONSTRAINTS"
        "text-input-unstable-v3|unstable/text-input/text-input-unstable-v3.xml|TEXT_INPUT_V3"
        "primary-selection-unstable-v1|unstable/primary-selection/primary-selection-unstable-v1.xml|PRIMARY_SELECTION"
        "xdg-decoration-unstable-v1|unstable/xdg-decoration/xdg-decoration-unstable-v1.xml|XDG_DECORATION"
        "xdg-activation-v1|staging/xdg-activation/xdg-activation-v1.xml|XDG_ACTIVATION"
        "idle-inhibit-unstable-v1|unstable/idle-inhibit/idle-inhibit-unstable-v1.xml|IDLE_INHIBIT"
        "xdg-foreign-unstable-v2|unstable/xdg-foreign/xdg-foreign-unstable-v2.xml|XDG_FOREIGN")
    foreach(_entry IN LISTS _optional)
        string(REPLACE "|" ";" _parts "${_entry}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _relative)
        list(GET _parts 2 _define)
        if(EXISTS "${_protocols_dir}/${_relative}")
            list(APPEND _protocols "${_name};${_protocols_dir}/${_relative}")
            list(APPEND _definitions "CNA_WAYLAND_HAVE_${_define}=1")
            list(APPEND _found_optional "${_name}")
        else()
            list(APPEND _missing_optional "${_name}")
        endif()
    endforeach()

    # tablet-v2 carries the graphics tablets (WAYLAND-0059) and is also what cursor-shape-v1
    # needs: cursor-shape names zwp_tablet_tool_v2 in one of its requests, so its generated code
    # refers to that interface and the two are generated together. tablet-v2 became stable in
    # wayland-protocols 1.43 and was unstable before, so both paths are looked for.
    set(_tablet "")
    foreach(_candidate stable/tablet/tablet-v2.xml unstable/tablet/tablet-unstable-v2.xml)
        if(NOT _tablet AND EXISTS "${_protocols_dir}/${_candidate}")
            set(_tablet "${_protocols_dir}/${_candidate}")
        endif()
    endforeach()
    if(_tablet)
        list(APPEND _protocols "tablet-v2;${_tablet}")
        list(APPEND _definitions "CNA_WAYLAND_HAVE_TABLET=1")
        list(APPEND _found_optional "tablet-v2")
    else()
        list(APPEND _missing_optional "tablet-v2")
    endif()
    if(_tablet AND EXISTS "${_protocols_dir}/staging/cursor-shape/cursor-shape-v1.xml")
        list(APPEND _protocols "cursor-shape-v1;${_protocols_dir}/staging/cursor-shape/cursor-shape-v1.xml")
        list(APPEND _definitions "CNA_WAYLAND_HAVE_CURSOR_SHAPE=1")
        list(APPEND _found_optional "cursor-shape-v1")
    else()
        list(APPEND _missing_optional "cursor-shape-v1")
    endif()

    # --- optional run-time libraries, headers only ------------------------------------------------

    # EGL: libEGL and libwayland-egl are opened at run time, exactly as the X11 backend opens
    # GLX (NPV-0121), so a HEADLESS or SOFTWARE Wayland build needs no GL implementation to start.
    pkg_check_modules(_cna_egl QUIET egl)
    pkg_check_modules(_cna_wl_egl QUIET wayland-egl)
    if(_cna_egl_FOUND AND _cna_wl_egl_FOUND)
        list(APPEND _includes ${_cna_egl_INCLUDE_DIRS} ${_cna_wl_egl_INCLUDE_DIRS})
        list(APPEND _definitions "CNA_WAYLAND_HAVE_EGL=1")
        list(APPEND _found_optional "EGL headers")
    else()
        list(APPEND _missing_optional "EGL headers")
    endif()

    pkg_check_modules(_cna_wl_cursor QUIET wayland-cursor)
    if(_cna_wl_cursor_FOUND)
        list(APPEND _includes ${_cna_wl_cursor_INCLUDE_DIRS})
        list(APPEND _definitions "CNA_WAYLAND_HAVE_CURSOR_THEME=1")
        list(APPEND _found_optional "wayland-cursor headers")
    else()
        list(APPEND _missing_optional "wayland-cursor headers")
    endif()

    find_package(Vulkan QUIET)
    if(Vulkan_INCLUDE_DIRS)
        list(APPEND _includes ${Vulkan_INCLUDE_DIRS})
        list(APPEND _definitions "CNA_WAYLAND_HAVE_VULKAN_HEADERS=1")
        list(APPEND _found_optional "Vulkan headers")
    else()
        list(APPEND _missing_optional "Vulkan headers")
    endif()

    pkg_check_modules(_cna_dbus QUIET dbus-1)
    if(_cna_dbus_FOUND)
        list(APPEND _includes ${_cna_dbus_INCLUDE_DIRS})
        list(APPEND _definitions "CNA_PLATFORM_HAVE_DBUS=1")
        list(APPEND _found_optional "D-Bus headers")
    else()
        list(APPEND _missing_optional "D-Bus headers")
    endif()

    # The test compositor (WAYLAND-0111) is a libwayland-server program inside the test binary.
    # Never linked into cna_platform.
    pkg_check_modules(_cna_wl_server QUIET wayland-server)
    if(_cna_wl_server_FOUND)
        set(CNA_WAYLAND_SERVER_LIBRARIES "${_cna_wl_server_LINK_LIBRARIES}" PARENT_SCOPE)
    endif()

    list(REMOVE_DUPLICATES _includes)
    list(JOIN _found_optional ", " _found_text)
    list(JOIN _missing_optional ", " _missing_text)
    set(_summary "wayland-client ${_cna_wl_client_VERSION}, xkbcommon ${_cna_xkb_VERSION}, \
wayland-protocols ${_cna_wl_protocols_VERSION}; optional present: ${_found_text}")
    if(_missing_optional)
        string(APPEND _summary "; absent: ${_missing_text}")
    endif()

    set(CNA_WAYLAND_AVAILABLE TRUE PARENT_SCOPE)
    set(CNA_WAYLAND_LIBRARIES "${_libraries}" PARENT_SCOPE)
    set(CNA_WAYLAND_INCLUDE_DIRS "${_includes}" PARENT_SCOPE)
    set(CNA_WAYLAND_DEFINITIONS "${_definitions}" PARENT_SCOPE)
    set(CNA_WAYLAND_SCANNER "${_scanner}" PARENT_SCOPE)
    set(CNA_WAYLAND_PROTOCOLS "${_protocols}" PARENT_SCOPE)
    set(CNA_WAYLAND_SUMMARY "${_summary}" PARENT_SCOPE)
endfunction()

# Generates the protocol bindings into <binary dir>/generated/wayland-protocols (D-2):
#
#   <name>-client-protocol.h   what the backend includes
#   <name>-server-protocol.h   what the in-process test compositor includes
#   <name>-protocol.c          the interface tables, as `private-code`: hidden visibility, so a
#                              host linking its own copy of, say, xdg_wm_base_interface (a
#                              toolkit, libdecor) cannot collide with CNA's
#
# Must be called in the directory whose target compiles the returned sources (custom command
# outputs belong to the directory that defines them).
#
#   out_sources      receives the generated .c files and headers, to add to a target
#   out_include_dir  receives the directory to put on that target's include path
function(cna_wayland_generate_protocols out_sources out_include_dir)
    set(_directory "${CMAKE_BINARY_DIR}/generated/wayland-protocols")
    file(MAKE_DIRECTORY "${_directory}")
    set(_sources "")
    set(_pairs ${CNA_WAYLAND_PROTOCOLS})
    list(LENGTH _pairs _count)
    math(EXPR _last "${_count} - 1")
    foreach(_index RANGE 0 ${_last} 2)
        math(EXPR _xml_index "${_index} + 1")
        list(GET _pairs ${_index} _name)
        list(GET _pairs ${_xml_index} _xml)
        set(_client "${_directory}/${_name}-client-protocol.h")
        set(_server "${_directory}/${_name}-server-protocol.h")
        set(_code "${_directory}/${_name}-protocol.c")
        add_custom_command(
            OUTPUT "${_client}" "${_server}" "${_code}"
            COMMAND "${CNA_WAYLAND_SCANNER}" client-header "${_xml}" "${_client}"
            COMMAND "${CNA_WAYLAND_SCANNER}" server-header "${_xml}" "${_server}"
            COMMAND "${CNA_WAYLAND_SCANNER}" private-code "${_xml}" "${_code}"
            DEPENDS "${_xml}"
            COMMENT "wayland-scanner: ${_name}"
            VERBATIM)
        list(APPEND _sources "${_client}" "${_server}" "${_code}")
    endforeach()
    set(${out_sources} "${_sources}" PARENT_SCOPE)
    set(${out_include_dir} "${_directory}" PARENT_SCOPE)
endfunction()
