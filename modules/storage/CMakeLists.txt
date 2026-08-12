file(GLOB CNA_STORAGE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_storage Storage ${CNA_STORAGE_SOURCES})
# StorageDevice/StorageContainer's public API names Microsoft::Xna::Framework::PlayerIndex
# and the headers use the CNAEXT marker + the header-only PathContainment internals -- all
# owned by the core module. The monolithic global include root used to satisfy this
# silently; the physical layout states the real include-contract edge instead. Headers-only
# (no core symbol referenced), so storage's accepted link closure is unchanged.
target_link_libraries(cna_storage PUBLIC cna_core_headers)
cna_link_sharp_runtime(cna_storage PUBLIC Core.Base IO Runtime Threading)
# plan_platform.md PLAT-109: StorageDevice resolves its save root through the platform
# filesystem service instead of SDL_GetPrefPath, so this module no longer links SDL.
target_link_libraries(cna_storage PRIVATE cna_platform)
