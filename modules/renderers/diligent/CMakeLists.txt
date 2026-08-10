# plan_diligent.md DILIGENT-2: SDL3 provides the window (and the native handle Diligent's swap
# chain is created from); every graphics call goes through the Diligent engine targets that
# cna_configure_diligent() actually built for this platform.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3)
cna_link_diligent(${BACKEND_TARGET})
