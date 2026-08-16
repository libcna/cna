file(GLOB_RECURSE CNA_GRAPHICS_CORE_SOURCES CONFIGURE_DEPENDS
    "src/Xna/*.cpp"
    "src/Internal/*.cpp"
)
cna_add_module(cna_graphics_core GraphicsCore ${CNA_GRAPHICS_CORE_SOURCES})
# IGraphicsRenderer's window registry is keyed by the platform contract's WindowId. Public is
# intentional: renderer modules include that common header and therefore need the platform include
# interface too; the platform module itself depends only on cna_core_headers, so this adds no cycle.
target_link_libraries(cna_graphics_core PUBLIC cna_math cna_core cna_platform)
cna_link_sharp_runtime(cna_graphics_core PUBLIC Core.Base IO Collections.Core Text)
# PLAT-65: generic image decode/encode uses CNA's already-vendored single-header stb copy. Keep it
# private: no stb type crosses ImageLoader's RGBA8 boundary.
# CNA_SOURCE_DIR, not CMAKE_SOURCE_DIR: stb is vendored in *this* repository, and CMAKE_SOURCE_DIR
# is the consumer's root whenever CNA is used via add_subdirectory (cna-gltf-viewer and
# mobile-eggbert both do), where it resolved to a third_party/stb that does not exist.
target_include_directories(cna_graphics_core PRIVATE ${CNA_SOURCE_DIR}/third_party/stb)
# Genuine XNA-semantic static-archive cycle (kept, not redesigned): GraphicsDevice updates
# TouchPanel display metrics and Mouse/TextInputEXT window binding, while Input::MouseCursor
# builds on Graphics::Texture2D. Declared on both targets so CMake repeats the archives.
target_link_libraries(cna_graphics_core PRIVATE cna_input)
# GraphicsDevice.cpp constructs the selected renderer through
# CNA::Internal::Renderers::CreateGraphicsRenderer(), whose definition lives in the renderer
# archive. The umbrella's INTERFACE membership is not a link-order constraint, so the forward
# edge must be declared on the graphics core itself; together with the reverse edges declared
# by the renderer modules this closes the graphics-core <-> renderer cycle for the renderers
# that have one, and a plain ordered dependency for the rest.
target_link_libraries(cna_graphics_core PRIVATE ${RENDERER_TARGET})

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
