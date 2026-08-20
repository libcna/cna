# plans/plan_dx8.md design decision 2/14: mingw-w64's x86_64 target ships NO real d3d8 import
# library at all (only the unrelated libd3d8thk.a "thunk" library; only the i686/32-bit target
# has a real one) -- DXVK's own d3d8.dll.a (D8VK, merged into DXVK 2.0+) exports the real
# Direct3DCreate8 symbol and is linked against directly instead, spike-confirmed (DX8-0a).
cna_add_renderer()
set(CNA_DX8_DXVK_LIB "/usr/lib/dxvk/wine64/d3d8.dll.a" CACHE FILEPATH
    "Path to DXVK's own d3d8 import library (exports Direct3DCreate8) -- no MinGW x86_64 import library exists for real d3d8.")
if(NOT EXISTS "${CNA_DX8_DXVK_LIB}")
    message(FATAL_ERROR
        "CNA: DIRECTX8 renderer requires DXVK's own d3d8 import library, not found at "
        "${CNA_DX8_DXVK_LIB} -- install the dxvk package (providing /usr/lib/dxvk/wine64/"
        "d3d8.dll.a) or set -DCNA_DX8_DXVK_LIB=<path> to point at it explicitly.")
endif()
target_link_libraries(${RENDERER_TARGET} PRIVATE "${CNA_DX8_DXVK_LIB}")

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
