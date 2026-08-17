cna_add_renderer()
# cmake/ThirdPartyLLGL.cmake configured LLGL as a STATIC library, so the renderer modules
# (LLGL_OpenGL/LLGL_Vulkan/LLGL_Null) are separate archives that LLGL's static module registry
# resolves by name at runtime -- they are not transitive dependencies of the core LLGL target
# and must all reach the final link. CNA_LLGL_LIBRARIES is that complete list, published by
# cna_configure_llgl(). PUBLIC (not PRIVATE) because every test/example executable links the
# renderer target directly and needs the same archives on its own link line.
#
# The core archive and its modules genuinely reference each other -- LLGL's static module
# registry calls into LLGL_OpenGL/LLGL_Vulkan/LLGL_Null, and those call back into the core --
# and CMake orders the modules ahead of the core, so a single left-to-right pass leaves the
# registry's calls unresolved (found empirically: "undefined reference to
# LLGL::ModuleOpenGL::AllocRenderSystem" from StaticModuleInterface.cpp). A link group makes
# the linker rescan the set until it converges -- the same mechanism the per-renderer test
# macros already use for the CNA/renderer cycle.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
    string(JOIN "," CNA_LLGL_LINK_GROUP_MEMBERS ${CNA_LLGL_LIBRARIES})
    target_link_libraries(${RENDERER_TARGET} PUBLIC
        "$<LINK_GROUP:RESCAN,${CNA_LLGL_LINK_GROUP_MEMBERS}>")
else()
    target_link_libraries(${RENDERER_TARGET} PUBLIC ${CNA_LLGL_LIBRARIES})
endif()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)

# plan_runtimerenderer.md RTR-P6-8: the LLGL module set, applied to THIS target rather than to the
# whole project. cna_configure_llgl() used to call add_compile_definitions(), which is directory-
# scoped even from inside a function, so every renderer's target in a multi-renderer build was told
# which LLGL modules exist. PUBLIC because LlglRendererSelection.hpp is a public header of this
# module and every test/example that links this target compiles against it.
if(CNA_LLGL_COMPILE_DEFINITIONS)
    target_compile_definitions(${RENDERER_TARGET} PUBLIC ${CNA_LLGL_COMPILE_DEFINITIONS})
endif()
# plan_llgl.md LLGL-27: LlglEffectRenderer needs a REAL runtime GLSL->SPIR-V compile for
# arbitrary user ShaderEffect source when the Vulkan module is selected at runtime (LLGL's own
# OpenGL module accepts the same GLSL text directly, no compiler needed there) -- the exact
# same problem another runtime-selected effect renderer already solved with libshaderc, and the
# same find_library()-then-glob-fallback this environment's own libshaderc1-only (no -dev
# package) install needs. Duplicated rather than shared with the sdl-gpu module: only the one
# selected renderer module is ever configured, so the other module's variable is never set here.
find_library(CNA_SHADERC_LIBRARY NAMES shaderc_shared shaderc)
if(NOT CNA_SHADERC_LIBRARY)
    file(GLOB CNA_SHADERC_CANDIDATES
        /usr/lib/*/libshaderc.so*
        /usr/lib/libshaderc.so*
        /usr/local/lib/libshaderc.so*
    )
    if(CNA_SHADERC_CANDIDATES)
        list(GET CNA_SHADERC_CANDIDATES 0 CNA_SHADERC_LIBRARY)
    endif()
endif()
if(NOT CNA_SHADERC_LIBRARY)
    message(FATAL_ERROR "CNA: libshaderc not found (required for LLGL's runtime ShaderEffect GLSL compile, LLGL-27) -- install libshaderc1 or libshaderc-dev")
endif()
message(STATUS "CNA: using libshaderc at ${CNA_SHADERC_LIBRARY}")
target_link_libraries(${RENDERER_TARGET} PRIVATE "${CNA_SHADERC_LIBRARY}")

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
