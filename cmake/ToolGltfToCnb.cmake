# plans/plan_cnb.md CNBF-106: direct glTF -> .cnb compilation.
#
# CP-009 moved the same implementation behind CNA::Content so this focused legacy front end and
# cna-content share one linked copy. A second glTF parser is the failure mode this avoids.
add_executable(cna_tool_gltf_to_cnb
    tools/gltf_to_cnb/gltf_to_cnb.cpp
)
target_include_directories(cna_tool_gltf_to_cnb PRIVATE
    tools/common
    tools/gltf_to_cnj
)
target_link_libraries(cna_tool_gltf_to_cnb
    PRIVATE
    CNA
)
cna_link_sharp_runtime(cna_tool_gltf_to_cnb PRIVATE)

if(CNA_DRACO_AVAILABLE)
    target_link_libraries(cna_tool_gltf_to_cnb PRIVATE cna_draco)
endif()
