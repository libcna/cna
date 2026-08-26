# plans/plan_cnb.md CNBF-063 (Phase D): the standalone offline .cnj -> .cnb content compiler.
# Not a test or an example -- a developer content tool, so it is built unconditionally, exactly
# like cna_tool_gltf_to_cnj (cmake/ToolGltfToCnj.cmake) whose wiring this mirrors. It links CNA
# and its declared sharp-runtime component closure and never constructs a GraphicsDevice: content
# compilation must not require a window, a GPU or a renderer to be available.
add_executable(cna_tool_cnj_to_cnb
    tools/cnj_to_cnb/cnj_to_cnb.cpp
)
target_link_libraries(cna_tool_cnj_to_cnb
    PRIVATE
    CNA
)
cna_link_sharp_runtime(cna_tool_cnj_to_cnb PRIVATE)
