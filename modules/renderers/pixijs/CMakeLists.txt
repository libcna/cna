# plan_pixijs.md Design decision 2: the browser platform owns the canvas element; this renderer
# consumes only the platform-neutral surface snapshot while actual rendering goes through
# EM_JS/PixiJS. It therefore links no windowing library at all, exactly like CANVAS and HTML_DOM.
cna_add_renderer()

# plan_pixijs.md Design decision 4: prepend the vendored PixiJS UMD build into the generated glue
# code, so PIXI.* is a real global before any CNA_PixiJs_* EM_JS function in this renderer runs.
# CNA_PIXIJS_JS_FILE is set by cna_configure_pixijs() (cmake/ThirdPartyPixiJS.cmake), called from
# cmake/RendererSelection.cmake's own per-identity dispatch -- which runs for PIXIJS whether it is
# the default renderer or merely one member of a multi-renderer set, so the flag is applied in both
# modes. PUBLIC so it reaches every executable that links this static library transitively, not
# just this target's own (nonexistent) direct link step.
#
# PIXIJS-86: --extern-pre-js, NOT --pre-js. --pre-js content is spliced into the module body and is
# then handed to Emscripten's own JS optimizer, which at -O2/-O3 treats the vendored bundle as
# ordinary generated code and dead-code-eliminates parts of it: a real -O3 build of
# cna_test_pixijs_smoke lost one of pixi.min.js's own top-level declarations and the page died with
# "ReferenceError: Pp is not defined" before a single check ran. --extern-pre-js is emitted verbatim,
# outside the optimizer, which is exactly the contract a third-party UMD bundle needs. This did not
# surface earlier because the only previous Emscripten build of this renderer was unoptimized.
if(EMSCRIPTEN AND CNA_PIXIJS_JS_FILE)
    target_link_options(${RENDERER_TARGET} PUBLIC "SHELL:--extern-pre-js \"${CNA_PIXIJS_JS_FILE}\"")
endif()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
