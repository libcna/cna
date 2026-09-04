# The platform surface snapshot provides the native window Diligent's swap chain is created from;
# every graphics call goes through the Diligent engine targets built for this platform.
cna_add_renderer()
cna_link_diligent(${RENDERER_TARGET})

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
