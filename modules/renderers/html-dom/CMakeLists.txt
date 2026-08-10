# plan_html_dom.md design decision 2: reuses the SDL-created window/canvas element for sizing,
# input and events, so SDL3 is still linked even though every draw goes through EM_JS/DOM/CSS
# and no SDL_Renderer is ever created.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)
