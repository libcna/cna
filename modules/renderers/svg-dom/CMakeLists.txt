# plan_svg_dom.md design decision 1: reuses the SDL-created window/canvas element for sizing,
# input and events, so SDL3 is still linked even though every draw goes through EM_JS/real SVG DOM
# elements and no SDL_Renderer is ever created. SDL3_image backs the PNG encode
# (SvgDomTextureRenderer::EncodePngEXT) this renderer's data-URI texture path uses.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 SDL3_image::SDL3_image)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
