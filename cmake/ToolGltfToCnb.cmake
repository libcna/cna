# plans/plan_cnb.md CNBF-106: direct glTF -> .cnb compilation.
#
# The important line here is the second source file. This tool compiles
# tools/gltf_to_cnj/gltf_to_cnj.cpp -- the SAME translation unit the .cnj tool is built from, with
# its main() suppressed -- rather than a copy of its logic. That is what makes "both formats
# interpret glTF identically" true by construction: there is one implementation, and both
# front-ends call it. A second glTF parser is the failure mode this task exists to avoid.
add_executable(cna_tool_gltf_to_cnb
    tools/gltf_to_cnb/gltf_to_cnb.cpp
    tools/gltf_to_cnj/gltf_to_cnj.cpp
    modules/content/tests/CNA/Internal/GltfImport/GltfOracleEXT.cpp
)
target_compile_definitions(cna_tool_gltf_to_cnb PRIVATE CNA_GLTF_TO_CNJ_NO_MAIN=1)
target_include_directories(cna_tool_gltf_to_cnb PRIVATE
    third_party/cgltf
    tools/gltf_to_cnj
    modules/content/tests/CNA/Internal/GltfImport
)
target_link_libraries(cna_tool_gltf_to_cnb
    PRIVATE
    CNA
)
cna_link_sharp_runtime(cna_tool_gltf_to_cnb PRIVATE)

if(CNA_DRACO_AVAILABLE)
    target_link_libraries(cna_tool_gltf_to_cnb PRIVATE cna_draco)
endif()
