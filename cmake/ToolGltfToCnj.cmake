# plan_cnj.md CNB-50/51/52 (Phase 12): standalone offline glTF 2.0 -> .cnj Model/AnimationClip
# converter. Not a test/example -- a developer content tool, so built unconditionally rather than
# gated behind CNA_BUILD_TESTS (matches cna_diag_compare's own "standalone tool, not wired into
# ctest" precedent in Harnesses.cmake). Links against CNA and its declared sharp-runtime component
# closure for Matrix/Vector3/Quaternion/TimeSpan only -- no graphics renderer or window/device
# initialization is ever triggered by this tool, since it only constructs plain math value types.
add_executable(cna_tool_gltf_to_cnj
    tools/gltf_to_cnj/gltf_to_cnj.cpp
    modules/content/tests/CNA/Internal/GltfImport/GltfOracleEXT.cpp
)
target_include_directories(cna_tool_gltf_to_cnj PRIVATE
    third_party/cgltf
    modules/content/tests/CNA/Internal/GltfImport
)
target_link_libraries(cna_tool_gltf_to_cnj
    PRIVATE
    CNA
)
cna_link_sharp_runtime(cna_tool_gltf_to_cnj PRIVATE)

# `--dump-oracle` decodes extension-backed POSITION data independently for its L4 world-space
# oracle. CNA intentionally keeps its own Draco dependency PRIVATE, so the diagnostic translation
# unit needs the optional decoder's headers and library explicitly as well.
if(CNA_DRACO_AVAILABLE)
    target_link_libraries(cna_tool_gltf_to_cnj PRIVATE cna_draco)
endif()
