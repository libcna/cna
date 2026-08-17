# plan_pixijs.md Design decision 2: reuses the existing SDL-created window/canvas element, so still
# links SDL3 even though actual rendering goes through EM_JS/PixiJS, not SDL_Renderer.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)

# plan_pixijs.md Design decision 4: prepend the vendored PixiJS UMD build into the generated glue
# code via --pre-js, so PIXI.* is a real global before any CNA_PixiJs_* EM_JS function in this
# renderer runs. CNA_PIXIJS_JS_FILE is set by cna_configure_pixijs() (cmake/ThirdPartyPixiJS.cmake),
# called from cmake/RendererSelection.cmake's own PIXIJS dispatch block before this directory is
# entered. PUBLIC so the flag reaches every executable that links this static library transitively,
# not just this target's own (nonexistent) direct link step.
if(EMSCRIPTEN AND CNA_PIXIJS_JS_FILE)
    target_link_options(${RENDERER_TARGET} PUBLIC "SHELL:--pre-js \"${CNA_PIXIJS_JS_FILE}\"")
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
