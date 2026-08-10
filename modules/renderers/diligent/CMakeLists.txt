# plan_diligent.md DILIGENT-2: SDL3 provides the window (and the native handle Diligent's swap
# chain is created from); every graphics call goes through the Diligent engine targets that
# cna_configure_diligent() actually built for this platform.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3)
cna_link_diligent(${RENDERER_TARGET})

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
