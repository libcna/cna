# plans/plan_stub.md: deliberately minimal no-op renderer -- renders nothing, touches no SDL
# window/video subsystem/GPU library.
cna_add_renderer()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
