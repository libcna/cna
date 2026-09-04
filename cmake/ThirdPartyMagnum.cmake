# --- Magnum / Corrade acquisition for the MAGNUM graphics renderer ---
#
# plans/plan_magnum.md MAGNUM-1/MAGNUM-2. Magnum is a real multi-repository C++ graphics middleware
# (mosra/corrade + mosra/magnum), not a single vendorable header like cgltf.h/stb_image.h, so it is
# acquired the same way BGFX already is: a pinned upstream revision, fetched on demand and built as
# part of this project's own build tree. Unlike BGFX it needs TWO repositories -- Magnum does not
# build at all without Corrade, its own base library -- and Corrade must be made available first
# because Magnum's build runs Corrade's `corrade-rc` resource compiler as a host tool.
#
# Three acquisition routes, tried in order, mirroring cmake/ThirdPartyWebGPU.cmake's own
# "prefer a reproducible local checkout, fall back to a download" shape:
#
#   1. CNA_MAGNUM_ROOT  -- an existing Corrade+Magnum INSTALL prefix (offline/reproducible builds
#                          and CI images that already ship one). Consumed via find_package.
#   2. A system-wide Corrade+Magnum install already on CMAKE_PREFIX_PATH.
#   3. FetchContent of the pinned revisions below (the default for a fresh checkout).
#
# Route 3 builds Magnum as a static library inside the selected cmake-build-<variant>/ directory,
# so it is built once per build directory and incrementally reused afterwards, exactly like bgfx.

include_guard(GLOBAL)

# Pinned upstream revisions. Magnum's last tagged release (v2020.06) predates the C++20/23 toolchains
# CNA builds with and does not compile cleanly against them; upstream's own recommendation is to
# track master, so these are the exact master revisions this renderer was developed and verified
# against rather than a moving branch name.
set(CNA_CORRADE_GIT_TAG "783e4e4807536ec52c352986fc9317db986ace96"
    CACHE STRING "Corrade revision fetched for the MAGNUM graphics renderer")
set(CNA_MAGNUM_GIT_TAG "5a7424643bfd4621fbcff8c361d37795502cf890"
    CACHE STRING "Magnum revision fetched for the MAGNUM graphics renderer")

set(CNA_MAGNUM_ROOT "" CACHE PATH
    "Existing Corrade+Magnum install prefix to use instead of downloading them")

# Which of Magnum's platform-specific GL context libraries supplies the OpenGL function loader for
# Platform::GLContext. Magnum has no generic "load through a caller-supplied getProcAddress" entry
# point -- the loader lives in exactly one of these four libraries -- so the choice is made here,
# per platform, rather than at runtime.
if(WIN32)
    set(_cna_magnum_context_component "WglContext")
elseif(APPLE)
    set(_cna_magnum_context_component "CglContext")
else()
    option(CNA_MAGNUM_USE_EGL
        "Load OpenGL entry points through EGL instead of GLX in the MAGNUM renderer" OFF)
    if(CNA_MAGNUM_USE_EGL)
        set(_cna_magnum_context_component "EglContext")
    else()
        set(_cna_magnum_context_component "GlxContext")
    endif()
endif()
set(CNA_MAGNUM_CONTEXT_COMPONENT "${_cna_magnum_context_component}" CACHE INTERNAL
    "Magnum platform GL context component the MAGNUM renderer links")

function(_cna_magnum_set_build_options)
    # Only the pieces the renderer actually uses. Magnum's default configuration builds a large set
    # of libraries, plugins and utilities CNA has no consumer for; every one of them left ON is
    # build time paid on every fresh build directory for nothing.
    set(BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(CORRADE_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(CORRADE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CORRADE_BUILD_DEPRECATED OFF CACHE BOOL "" FORCE)
    set(CORRADE_WITH_INTERCONNECT OFF CACHE BOOL "" FORCE)
    set(CORRADE_WITH_TESTSUITE OFF CACHE BOOL "" FORCE)

    set(MAGNUM_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(MAGNUM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MAGNUM_BUILD_DEPRECATED OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_GL ON CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_DEBUGTOOLS OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_MESHTOOLS OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_PRIMITIVES OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_SCENEGRAPH OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_SCENETOOLS OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_SHADERS OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_SHADERTOOLS OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_TEXT OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_TEXTURETOOLS OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_TRADE OFF CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_MATERIALTOOLS OFF CACHE BOOL "" FORCE)

    if(WIN32)
        set(MAGNUM_WITH_WGLCONTEXT ON CACHE BOOL "" FORCE)
    elseif(APPLE)
        set(MAGNUM_WITH_CGLCONTEXT ON CACHE BOOL "" FORCE)
    elseif(CNA_MAGNUM_USE_EGL)
        set(MAGNUM_WITH_EGLCONTEXT ON CACHE BOOL "" FORCE)
    else()
        set(MAGNUM_WITH_GLXCONTEXT ON CACHE BOOL "" FORCE)
    endif()
endfunction()

function(cna_configure_magnum)
    if(TARGET Magnum::GL)
        return()
    endif()

    # Route 1/2: an existing install prefix.
    if(CNA_MAGNUM_ROOT)
        list(APPEND CMAKE_PREFIX_PATH "${CNA_MAGNUM_ROOT}")
        list(APPEND CMAKE_MODULE_PATH "${CNA_MAGNUM_ROOT}/share/cmake/Corrade"
                                      "${CNA_MAGNUM_ROOT}/share/cmake/Magnum")
    endif()
    find_package(Magnum QUIET COMPONENTS GL ${CNA_MAGNUM_CONTEXT_COMPONENT})
    if(Magnum_FOUND)
        message(STATUS "CNA: using prebuilt Magnum (${CNA_MAGNUM_CONTEXT_COMPONENT} context)")
        return()
    endif()
    if(CNA_MAGNUM_ROOT)
        message(FATAL_ERROR
            "CNA: CNA_MAGNUM_ROOT is set to '${CNA_MAGNUM_ROOT}' but no Corrade+Magnum install "
            "with the ${CNA_MAGNUM_CONTEXT_COMPONENT} component was found there. Either point it "
            "at a real install prefix or unset it to let CMake fetch and build Magnum instead.")
    endif()

    # Route 3: fetch and build. FETCHCONTENT_SOURCE_DIR_CORRADE / _MAGNUM (CMake's own built-in
    # override) still let an offline build point these at local checkouts without an install step.
    message(STATUS "CNA: fetching Corrade+Magnum (${CNA_MAGNUM_CONTEXT_COMPONENT} context)")
    include(FetchContent)
    _cna_magnum_set_build_options()

    FetchContent_Declare(corrade
        GIT_REPOSITORY https://github.com/mosra/corrade.git
        GIT_TAG ${CNA_CORRADE_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(corrade)

    # Magnum's own build looks Corrade up with find_package() even when Corrade is already part of
    # the same build tree, and resolves its `corrade-rc` host tool through CORRADE_RC_EXECUTABLE.
    # Both are satisfied from the in-tree Corrade rather than from an install prefix that does not
    # exist in this route.
    list(APPEND CMAKE_MODULE_PATH "${corrade_SOURCE_DIR}/modules")
    set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
    set(CORRADE_RC_EXECUTABLE "$<TARGET_FILE:Corrade::rc>" CACHE FILEPATH "" FORCE)

    FetchContent_Declare(magnum
        GIT_REPOSITORY https://github.com/mosra/magnum.git
        GIT_TAG ${CNA_MAGNUM_GIT_TAG}
        GIT_SHALLOW FALSE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(magnum)

    list(APPEND CMAKE_MODULE_PATH "${magnum_SOURCE_DIR}/modules")
    set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
endfunction()
