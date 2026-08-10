# plan_freedirect.md design decision 10: free-direct's own public target is the literal lowercase
# `free-direct` (PUBLIC-links free-api::free-api, PUBLIC-exposes its own include/ dir so
# #include <ddraw.h> resolves) -- no ALIAS namespace exists for it, unlike easy-gl's own target.
cna_add_renderer_backend()
target_link_libraries(${BACKEND_TARGET} PRIVATE SDL3::SDL3 free-direct)
