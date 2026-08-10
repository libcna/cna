# plan_canvas.md design decision 2: reuses the existing SDL-created window/canvas element,
# so still links SDL3 even though actual rendering goes through EM_JS/Canvas2D, not SDL_Renderer.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)
