include_guard(GLOBAL)

# IGL (https://github.com/facebook/igl) integration for the IGL graphics renderer.
#
# IGL -- Meta's "Intermediate Graphics Library" -- is itself an abstraction over OpenGL/OpenGL ES,
# Vulkan and Metal, so like LLGL (and unlike every renderer that names a native API) CNA does not
# talk to a graphics API here: it talks to igl::IDevice, and which backend that device wraps is
# decided per process rather than per translation unit. The set of backends compiled in here is
# therefore a real capability boundary -- a backend that was not built can never be selected at
# runtime, no matter what CNA_IGL_BACKEND asks for (see IglRendererSelection.cpp).
#
# Acquisition follows the pinned-tag FetchContent shape used by LLGL/BGFX, with the same explicit
# local-source escape hatch for reproducible/offline builds.

set(CNA_IGL_GIT_TAG "v1.1.1" CACHE STRING
    "IGL git tag/commit to fetch (pinned; do not track a moving branch)")
set(CNA_IGL_GIT_REPOSITORY "https://github.com/facebook/igl.git" CACHE STRING
    "IGL git repository URL")
set(CNA_IGL_ROOT "" CACHE PATH
    "Path to an existing IGL source checkout; when set, no download is performed")

# IGL's Vulkan backend needs real Vulkan headers, so this defaults to whatever the host can
# honestly provide instead of hard-failing a machine without a Vulkan SDK -- identical shape to
# cmake/ThirdPartyLLGL.cmake's own Vulkan-module default.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    set(_cna_igl_vulkan_default ON)
else()
    set(_cna_igl_vulkan_default OFF)
endif()

option(CNA_IGL_BUILD_BACKEND_OPENGL
       "Build IGL's OpenGL backend (the default CNA IGL runtime)" ON)
option(CNA_IGL_BUILD_BACKEND_VULKAN
       "Build IGL's Vulkan backend" ${_cna_igl_vulkan_default})

# IGL's own top-level CMakeLists runs deploy_deps.py, which fetches EVERY entry of
# third-party/bootstrap-deps.json -- glfw, imgui, tracy, ktx-software, gfxreconstruct and a dozen
# more that exist only for its samples/shell/tests. None of those reach IGLLibrary, IGLOpenGL,
# IGLVulkan or IGLGlslang, and downloading them costs well over a gigabyte of disk for nothing. CNA
# therefore drives the SAME bootstrap script itself with an explicit `-n <name>` list (bootstrap.py
# supports selecting individual libraries) and turns IGL_DEPLOY_DEPS off, so only the dependencies
# the four library targets genuinely include are ever fetched.
set(CNA_IGL_REQUIRED_DEPS glm fmt glslang SPIRV-Headers CACHE STRING
    "IGL third-party dependencies CNA fetches; the Vulkan-only ones are appended automatically")
set(CNA_IGL_REQUIRED_VULKAN_DEPS volk vma CACHE STRING
    "Additional IGL third-party dependencies needed only by IGL's Vulkan backend")

# Runs IGL's own third-party/bootstrap.py for exactly the named dependencies.
function(_cna_igl_deploy_deps igl_source_dir)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    set(_deps ${CNA_IGL_REQUIRED_DEPS})
    if(CNA_IGL_BUILD_BACKEND_VULKAN)
        list(APPEND _deps ${CNA_IGL_REQUIRED_VULKAN_DEPS})
    endif()

    set(_names)
    foreach(_dep IN LISTS _deps)
        list(APPEND _names "-n" "${_dep}")
    endforeach()

    message(STATUS "CNA: deploying IGL dependencies: ${_deps}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "third-party/bootstrap.py"
                -b "third-party/deps"
                "--bootstrap-file=third-party/bootstrap-deps.json"
                --break-on-first-error
                ${_names}
        WORKING_DIRECTORY "${igl_source_dir}"
        RESULT_VARIABLE _bootstrap_result)
    if(NOT _bootstrap_result EQUAL 0)
        message(FATAL_ERROR
            "CNA: IGL dependency bootstrap failed (exit ${_bootstrap_result}). Re-run it by hand "
            "with: cd ${igl_source_dir} && python3 third-party/bootstrap.py -b third-party/deps "
            "--bootstrap-file=third-party/bootstrap-deps.json ${_names}")
    endif()

    # A missing directory here means bootstrap.py reported success while fetching nothing, which
    # would otherwise surface much later as an unreadable include path inside IGL's own CMake.
    foreach(_dep IN LISTS _deps)
        if(NOT EXISTS "${igl_source_dir}/third-party/deps/src/${_dep}")
            message(FATAL_ERROR
                "CNA: IGL dependency '${_dep}' was not deployed to "
                "${igl_source_dir}/third-party/deps/src/${_dep}")
        endif()
    endforeach()
endfunction()

# Configures IGL and publishes the target list every consumer must link.
#
# IGL splits itself across four static libraries: IGLLibrary (the API surface), IGLOpenGL,
# IGLVulkan and IGLGlslang. IGLLibrary links the backends PUBLIC-ly itself, so unlike LLGL's static
# module registry there is no link-group/rescan problem here -- the single IGLLibrary target
# carries the whole closure.
function(cna_configure_igl)
    if(NOT CNA_IGL_BUILD_BACKEND_OPENGL AND NOT CNA_IGL_BUILD_BACKEND_VULKAN)
        message(FATAL_ERROR
            "CNA: the IGL renderer needs at least one backend. Enable CNA_IGL_BUILD_BACKEND_OPENGL "
            "(the default runtime) or CNA_IGL_BUILD_BACKEND_VULKAN.")
    endif()

    if(CNA_IGL_ROOT)
        if(NOT EXISTS "${CNA_IGL_ROOT}/CMakeLists.txt")
            message(FATAL_ERROR
                "CNA: CNA_IGL_ROOT='${CNA_IGL_ROOT}' does not contain a CMakeLists.txt -- it must "
                "point at an IGL source checkout (git clone ${CNA_IGL_GIT_REPOSITORY}).")
        endif()
        set(_igl_source_dir "${CNA_IGL_ROOT}")
        message(STATUS "CNA: using local IGL source at ${_igl_source_dir}")
    else()
        message(STATUS "CNA: fetching IGL ${CNA_IGL_GIT_TAG} from ${CNA_IGL_GIT_REPOSITORY}")
        include(FetchContent)
        FetchContent_Declare(
            igl
            GIT_REPOSITORY "${CNA_IGL_GIT_REPOSITORY}"
            GIT_TAG "${CNA_IGL_GIT_TAG}"
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE
            GIT_SUBMODULES ""
        )
        FetchContent_Populate(igl)
        set(_igl_source_dir "${igl_SOURCE_DIR}")
    endif()

    _cna_igl_deploy_deps("${_igl_source_dir}")

    # Only the library targets. Samples/shell/IGLU pull in glfw, imgui, stb, taskflow and the
    # 3D-Graphics-Rendering-Cookbook data set, none of which CNA links; tests pull in gtest and a
    # second EGL implementation. IGL_DEPLOY_DEPS is off because _cna_igl_deploy_deps above already
    # fetched exactly the four/six dependencies these targets include.
    set(IGL_WITH_SAMPLES OFF CACHE BOOL "" FORCE)
    set(IGL_WITH_SHELL OFF CACHE BOOL "" FORCE)
    set(IGL_WITH_IGLU OFF CACHE BOOL "" FORCE)
    set(IGL_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(IGL_WITH_TRACY OFF CACHE BOOL "" FORCE)
    set(IGL_WITH_TRACY_GPU OFF CACHE BOOL "" FORCE)
    set(IGL_WITH_OPENXR OFF CACHE BOOL "" FORCE)
    set(IGL_WITH_WEBGL OFF CACHE BOOL "" FORCE)
    set(IGL_WITH_OPENGLES OFF CACHE BOOL "" FORCE)
    set(IGL_DEPLOY_DEPS OFF CACHE BOOL "" FORCE)
    set(IGL_WITH_OPENGL ${CNA_IGL_BUILD_BACKEND_OPENGL} CACHE BOOL "" FORCE)
    set(IGL_WITH_VULKAN ${CNA_IGL_BUILD_BACKEND_VULKAN} CACHE BOOL "" FORCE)
    if(APPLE)
        set(IGL_WITH_METAL ON CACHE BOOL "" FORCE)
    else()
        set(IGL_WITH_METAL OFF CACHE BOOL "" FORCE)
    endif()

    # IGL's own top-level CMakeLists sets IGL_PLATFORM_LINUX_USE_EGL to 0 only when
    # IGL_WITH_SAMPLES or IGL_WITH_SHELL is ON, and to 1 otherwise -- but turning IGL_WITH_SAMPLES
    # ON is not the directory-scoped, EGL-define-only knob it looks like: IGL's CMakeLists guards
    # `add_subdirectory(samples/desktop)` (and the glfw/bc7enc/meshoptimizer/tinyobjloader/
    # ktx-software third-party subdirectories that target needs) behind that exact same variable,
    # unconditionally, so setting it ON to steer the EGL define also demands the ~1 GB of sample
    # dependencies design decision 7 exists specifically to avoid -- and CNA never fetches them, so
    # add_subdirectory fails outright. Leave IGL_WITH_SAMPLES OFF and correct the define afterwards
    # instead: relying on "the compiler keeps the last -D" is not safe here, because CMake does not
    # emit a consumed target's INTERFACE_COMPILE_DEFINITIONS in call order -- an append placed after
    # add_subdirectory() below was observed landing BEFORE IGL's own entry on the generated command
    # line, so the stale IGL_PLATFORM_LINUX_USE_EGL=1 silently won. Filter the stale entry out of
    # both the definitions IGLLibrary compiles itself with and the ones it hands to consumers, then
    # append the corrected value, so exactly one definition of this macro ever reaches the compiler.
    add_subdirectory("${_igl_source_dir}" igl EXCLUDE_FROM_ALL)

    if(NOT TARGET IGLLibrary)
        message(FATAL_ERROR
            "CNA: IGL was configured but published no IGLLibrary target -- the IGL version in use "
            "does not match this integration.")
    endif()

    if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
        # CNA's glx::Context is the only IGL Linux context that can adopt an existing on-screen
        # context (see docs/igl-renderer.md), so the GLX (non-EGL) path is required here.
        foreach(_prop COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
            get_target_property(_igl_defs IGLLibrary ${_prop})
            if(_igl_defs)
                list(FILTER _igl_defs EXCLUDE REGEX "^IGL_PLATFORM_LINUX_USE_EGL=")
            else()
                set(_igl_defs "")
            endif()
            list(APPEND _igl_defs "IGL_PLATFORM_LINUX_USE_EGL=0")
            set_target_properties(IGLLibrary PROPERTIES ${_prop} "${_igl_defs}")
        endforeach()
        unset(_igl_defs)
    endif()

    set(CNA_IGL_LIBRARIES IGLLibrary PARENT_SCOPE)
    set(CNA_IGL_SOURCE_DIR "${_igl_source_dir}" PARENT_SCOPE)

    # Mirrors the backend set into compile definitions so the renderer's own backend selection can
    # refuse a backend that was never built instead of failing deep inside igl::HWDevice.
    if(CNA_IGL_BUILD_BACKEND_OPENGL)
        add_compile_definitions(CNA_IGL_HAS_OPENGL)
    endif()
    if(CNA_IGL_BUILD_BACKEND_VULKAN)
        add_compile_definitions(CNA_IGL_HAS_VULKAN)
    endif()

    message(STATUS "CNA: IGL backends built: "
                   "OpenGL=${CNA_IGL_BUILD_BACKEND_OPENGL} Vulkan=${CNA_IGL_BUILD_BACKEND_VULKAN}")
endfunction()
