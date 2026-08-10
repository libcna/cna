file(GLOB_RECURSE CNA_GRAPHICS_CORE_SOURCES CONFIGURE_DEPENDS
    "src/Xna/*.cpp"
    "src/Internal/*.cpp"
)
cna_add_module(cna_graphics_core GraphicsCore ${CNA_GRAPHICS_CORE_SOURCES})
target_link_libraries(cna_graphics_core PUBLIC cna_math cna_core)
cna_link_sharp_runtime(cna_graphics_core PUBLIC Core.Base IO Collections.Core Text)
# GraphicsDevice.cpp/GraphicsAdapter.cpp/Texture2D.cpp query SDL video state directly;
# ImageLoader.cpp decodes through SDL3_image.
target_link_libraries(cna_graphics_core PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)
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
