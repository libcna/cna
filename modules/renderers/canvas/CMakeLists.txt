# The browser platform owns the canvas element; this renderer consumes only the surface snapshot
# while actual rendering goes through EM_JS/Canvas2D.
cna_add_renderer()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
