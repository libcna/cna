file(GLOB CNA_DESIGN_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
cna_add_module(cna_design Design ${CNA_DESIGN_SOURCES})
cna_link_sharp_runtime(cna_design PUBLIC ComponentModel)
target_link_libraries(cna_design PUBLIC cna_math)
