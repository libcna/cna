# plans/plan_headless.md: no GPU, no window, no native graphics SDK -- the renderer ends at
# the common interface and the declared reverse edges.
cna_add_renderer()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
