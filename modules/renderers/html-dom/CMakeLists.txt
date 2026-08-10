# plan_html_dom.md design decision 2: reuses the SDL-created window/canvas element for sizing,
# input and events, so SDL3 is still linked even though every draw goes through EM_JS/DOM/CSS
# and no SDL_Renderer is ever created.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
