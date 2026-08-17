file(GLOB_RECURSE CNA_GRAPHICS_CORE_SOURCES CONFIGURE_DEPENDS
    "src/Xna/*.cpp"
    "src/Internal/*.cpp"
)
# plan_runtimerenderer.md design decision 3: the generated renderer registry compiles into the
# graphics core, which is where GraphicsDevice consumes it. It lives in the build tree, not
# the source tree, so it is never committed and is regenerated whenever the selection changes.
cna_add_module(cna_graphics_core GraphicsCore
    ${CNA_GRAPHICS_CORE_SOURCES}
    ${CNA_RENDERER_REGISTRY_SOURCE})
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
# plan_runtimerenderer.md phase P6: link every selected renderer, not only the default. The
# declared archive cycles are per renderer, so each edge has to be declared individually.
target_link_libraries(cna_graphics_core PRIVATE ${CNA_RENDERER_TARGETS})

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
