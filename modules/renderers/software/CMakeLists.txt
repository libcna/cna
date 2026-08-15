if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE")
    # This wrapper includes SoftwareRenderer.cpp with CNA_SOFTWARE_2D_ONLY for the GDI
    # archive. The complete SOFTWARE archive compiles the implementation directly, once.
    file(GLOB _cna_software_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    list(REMOVE_ITEM _cna_software_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareRenderer2D.cpp")
    # plan_runtimerenderer.md RTR-P6-6: derive this family's own target rather than reading the
    # global scalar, which names the build DEFAULT and is only correct here because the caller
    # happens to have overwritten it first.
    cna_renderer_target_name(_cna_software_target)
    add_library(${_cna_software_target} STATIC ${_cna_software_sources})
    cna_renderer_common_setup(${_cna_software_target})
    set(RENDERER_TARGET "${_cna_software_target}")
elseif(CNA_GRAPHICS_RENDERER STREQUAL "GDI")
    # GDI consumes the CPU 2D implementation through a dedicated translation unit. It deliberately
    # compiles neither the Software 3D/cube resource implementations nor the 3D draw entry points;
    # the full SOFTWARE renderer compiles the remaining general implementation plus the shared 2D
    # units. Keep this dependency explicit: future Software translation units must not enter the
    # 2D-only GDI build without review. Physical ownership of these files stays with the software
    # module; compiling them into the GDI archive is the one documented target-membership
    # exception (see modules/CMakeLists.txt's validator note).
    set(CNA_GDI_SOFTWARE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareFramebufferAllocation.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareTextureAllocation.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareFramebuffer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareTexture2D.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareRenderTarget2D.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareRenderer2DState.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareSpriteBatch.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SoftwareRenderer2D.cpp"
    )
    set(CNA_GDI_SOFTWARE_SOURCES ${CNA_GDI_SOFTWARE_SOURCES} PARENT_SCOPE)
    # The GDI translation units include the software module's public-internal headers
    # (CNA/Internal/Renderers/Software/...) -- publish the include root as an interface target.
    add_library(cna_renderer_software_headers INTERFACE)
    target_include_directories(cna_renderer_software_headers INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
