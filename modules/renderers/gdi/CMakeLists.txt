# GDI presents the Software renderer's CPU framebuffer through classic Win32 GDI; the shared
# CPU-2D translation units come from the software module (CNA_GDI_SOFTWARE_SOURCES, published
# by modules/renderers/software/CMakeLists.txt in the GDI configuration).
cna_add_renderer(${CNA_GDI_SOFTWARE_SOURCES})
target_link_libraries(${RENDERER_TARGET} PRIVATE gdi32 cna_renderer_software_headers)
# Every shared unit is the 2D-only build, not just SoftwareRenderer2D.cpp, whose own #define used
# to be the only one: SoftwareRenderer2DState.cpp then compiled its cube-target branches too, and
# they reference cube renderer code this archive never builds. No header depends on the macro, so
# the class layout is the same in every unit.
target_compile_definitions(${RENDERER_TARGET} PRIVATE CNA_SOFTWARE_2D_ONLY=1)

# GDI-078: report the actual GDI archive boundary at configure time, so plans/plan_gdi.md/NEXT_gdi.md
# source/object counts can be checked against generated evidence instead of a copied number.
list(LENGTH CNA_GDI_SOFTWARE_SOURCES CNA_GDI_SHARED_SOURCE_COUNT)
math(EXPR CNA_GDI_ARCHIVE_TOTAL "${CNA_GDI_SHARED_SOURCE_COUNT} + 3")
message(STATUS
    "CNA: GDI renderer archive = ${CNA_GDI_SHARED_SOURCE_COUNT} shared CPU-2D sources + 3 "
    "GDI-owned units = ${CNA_GDI_ARCHIVE_TOTAL} translation units")

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
