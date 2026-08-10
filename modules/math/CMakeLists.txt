file(GLOB CNA_MATH_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_math Math ${CNA_MATH_SOURCES})
cna_link_sharp_runtime(cna_math PUBLIC Core.Base)
# The math headers use the CNAEXT marker macro from CNA/CNAHelper.hpp -- a headers-only
# dependency on the core module (no core symbol is referenced), so the link closure stays
# "math + sharp-runtime only" exactly as the minimal-link gate pins it.
target_link_libraries(cna_math PUBLIC cna_core_headers)
