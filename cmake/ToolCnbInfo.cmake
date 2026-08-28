# plans/plan_cnb.md CNBF-H013: the offline .cnb inspector and validator. A developer content tool, so
# built unconditionally like cna_tool_gltf_to_cnj and cna_tool_cnj_to_cnb whose wiring this mirrors.
# It links CNA only for the container reader -- no GraphicsDevice is ever constructed, so inspecting
# an asset needs no window, GPU or renderer.
add_executable(cna_tool_cnb_info
    tools/cnb_info/cnb_info.cpp
)
target_link_libraries(cna_tool_cnb_info
    PRIVATE
    CNA
)
cna_link_sharp_runtime(cna_tool_cnb_info PRIVATE)
