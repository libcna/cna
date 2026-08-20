# plans/plan_opengles1.md design decision 1: requires a REAL system OpenGL ES 1.1 (fixed-function
# "Common", CM) library and Khronos headers -- e.g. Debian/Ubuntu's libgles1 + libgles-dev
# (GLESv1_CM.so + GLES/gl.h + GLES/glext.h), or the equivalent on an embedded/Android SDK.
# Same "hard system dependency, not vendored, FATAL_ERROR with install instructions if
# missing" shape as VULKAN's find_package(Vulkan REQUIRED), not a soft find_library
# fallback -- this renderer cannot function at all without a genuine ES1 CM implementation.
cna_add_renderer()
find_library(CNA_GLESV1_CM_LIBRARY NAMES GLESv1_CM)
find_path(CNA_GLES_INCLUDE_DIR NAMES GLES/gl.h)
if(NOT CNA_GLESV1_CM_LIBRARY OR NOT CNA_GLES_INCLUDE_DIR)
    message(FATAL_ERROR
        "CNA: OpenGL ES 1.1 (GLESv1_CM) library/headers not found -- install libgles1 "
        "libgles-dev (Debian/Ubuntu), providing libGLESv1_CM.so plus GLES/gl.h and "
        "GLES/glext.h, or the equivalent on other distros/embedded SDKs.")
endif()
message(STATUS "CNA: using GLESv1_CM at ${CNA_GLESV1_CM_LIBRARY} (headers: ${CNA_GLES_INCLUDE_DIR})")
target_link_libraries(${RENDERER_TARGET} PRIVATE "${CNA_GLESV1_CM_LIBRARY}")
target_include_directories(${RENDERER_TARGET} PRIVATE "${CNA_GLES_INCLUDE_DIR}")

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
