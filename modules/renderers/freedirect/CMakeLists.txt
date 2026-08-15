# plan_freedirect.md design decision 10: free-direct's own public target is the literal lowercase
# `free-direct` (PUBLIC-links free-api::free-api, PUBLIC-exposes its own include/ dir so
# #include <ddraw.h> resolves) -- no ALIAS namespace exists for it, unlike easy-gl's own target.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE SDL3::SDL3 free-direct)
set_property(TARGET ${RENDERER_TARGET} PROPERTY REQUIRES_PLATFORM SDL3)

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
