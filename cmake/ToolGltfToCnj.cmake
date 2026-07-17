# plan_cnj.md CNB-50/51/52 (Phase 12): standalone offline glTF 2.0 -> .cnj Model/AnimationClip
# converter. Not a test/example -- a developer content tool, so built unconditionally rather than
# gated behind CNA_BUILD_TESTS (matches cna_diag_compare's own "standalone tool, not wired into
# ctest" precedent in Harnesses.cmake). Links against CNA/SHARP_RUNTIME for Matrix/Vector3/
# Quaternion/TimeSpan only -- no graphics backend or window/device initialization is ever
# triggered by this tool, since it only ever constructs plain math value types.
add_executable(cna_tool_gltf_to_cnj
    tools/gltf_to_cnj/gltf_to_cnj.cpp
)
target_include_directories(cna_tool_gltf_to_cnj PRIVATE third_party/cgltf)
target_link_libraries(cna_tool_gltf_to_cnj
    PRIVATE
    CNA
    SHARP_RUNTIME
)
