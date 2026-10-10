# =====================================================================================
# The sibling releases this CNA release is built and tested against (docs/releasing.md).
#
# sharp-runtime, easy-gl and meta-gl are separate git checkouts consumed with
# add_subdirectory -- ../sharp-runtime, ../easy-gl, and easy-gl's own ../meta-gl -- so checking
# out a CNA tag does not select them. Until 0.1.0 the revisions were only recorded in
# CHANGELOG.md, which documented the pin without enforcing it. Each sibling now generates a
# Version.hpp from its own one-place version, and cna_require_sibling_version() reads it right
# after add_subdirectory and refuses an incompatible checkout at configure time.
#
# "Compatible" follows the pre-1.0 rule all four projects state in their docs/releasing.md: a
# minor bump may break the API, a patch release may not. So a checkout satisfies a requirement
# X.Y.Z when it declares X.Y.Z or a later X.Y patch release, and not when it declares an
# X.Y.Z pre-release, which precedes X.Y.Z. The exact commits each release was verified against
# stay in CHANGELOG.md.
#
# A release bump updates these three values, the matching CHANGELOG.md entry, and nothing else.
# =====================================================================================

set(CNA_REQUIRED_SHARP_RUNTIME_VERSION "0.1.0")
set(CNA_REQUIRED_EASYGL_VERSION        "0.1.1")
set(CNA_REQUIRED_METAGL_VERSION        "0.4.1")

option(CNA_CHECK_DEPENDENCY_VERSIONS
    "Fail configuration when a sibling checkout (sharp-runtime, easy-gl, meta-gl) declares a version this CNA release does not support; OFF reports it as a warning instead"
    ON)

# cna_require_sibling_version(<name> <checkout> <generated Version.hpp> <macro> <required>)
#
# <generated Version.hpp> is the header the sibling configured into its own build directory, and
# <macro> its *_VERSION_STRING define. A checkout too old to generate one is reported as
# declaring no version.
function(cna_require_sibling_version name checkout header macro required)
    set(declared "")
    if(EXISTS "${header}")
        file(STRINGS "${header}" _line REGEX "^#define ${macro} \"[^\"]*\"$")
        string(REGEX REPLACE "^#define ${macro} \"([^\"]*)\"$" "\\1" declared "${_line}")
    endif()

    if(NOT required MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
        message(FATAL_ERROR "CNA: required ${name} version '${required}' is not MAJOR.MINOR.PATCH")
    endif()
    set(_major "${CMAKE_MATCH_1}")
    set(_minor "${CMAKE_MATCH_2}")
    set(_patch "${CMAKE_MATCH_3}")

    set(_compatible FALSE)
    if(declared MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
        set(_have_major "${CMAKE_MATCH_1}")
        set(_have_minor "${CMAKE_MATCH_2}")
        set(_have_patch "${CMAKE_MATCH_3}")
        set(_have_prerelease "${CMAKE_MATCH_4}")
        if(_have_major EQUAL _major AND _have_minor EQUAL _minor)
            if(_have_patch GREATER _patch)
                set(_compatible TRUE)
            elseif(_have_patch EQUAL _patch AND _have_prerelease STREQUAL "")
                set(_compatible TRUE)
            endif()
        endif()
    endif()

    if(_compatible)
        message(STATUS "CNA: ${name} ${declared} at ${checkout} (requires ${required})")
        return()
    endif()

    if(declared STREQUAL "")
        set(declared "no version (a checkout older than its first versioned release)")
    endif()
    set(_text
        "CNA ${CNA_VERSION_STRING} requires ${name} ${required}, or a later ${_major}.${_minor}.x "
        "patch release, but the checkout at ${checkout} declares ${declared}. Check out the "
        "release there (git -C \"${checkout}\" checkout v${required}), or configure with "
        "-DCNA_CHECK_DEPENDENCY_VERSIONS=OFF to build against it anyway with a warning.")
    string(CONCAT _text ${_text})
    if(CNA_CHECK_DEPENDENCY_VERSIONS)
        message(FATAL_ERROR "${_text}")
    else()
        message(WARNING "${_text}")
    endif()
endfunction()
