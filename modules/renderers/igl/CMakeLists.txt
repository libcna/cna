cna_add_renderer()

# cmake/ThirdPartyIGL.cmake configures IGL as a set of static libraries whose whole closure hangs
# off IGLLibrary (IGLOpenGL, IGLVulkan and IGLGlslang are linked PUBLIC by it), so -- unlike LLGL's
# static module registry -- one target is enough and no link group is needed. PUBLIC because every
# test and example executable links the renderer target directly and needs the same archives on its
# own link line.
target_link_libraries(${RENDERER_TARGET} PUBLIC ${CNA_IGL_LIBRARIES})

# The OpenGL backend adopts the GLX context CNA's own platform GL service creates for the window
# (see IglPlatformSurface.cpp), so this family needs the X11 client library on Linux. It is a system
# dependency, exactly as VULKAN's find_package(Vulkan REQUIRED) is, rather than something CNA
# vendors.
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(X11 REQUIRED)
    target_link_libraries(${RENDERER_TARGET} PRIVATE ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
    target_include_directories(${RENDERER_TARGET} PRIVATE ${X11_INCLUDE_DIR})
endif()

# Module-owned examples (module-examples campaign): the family's example/test registration is
# module-local.
add_subdirectory(examples)
