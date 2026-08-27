# plans/plan_cnb.md CNBF-109/CNBF-110/CNBF-112: the direct source -> .cnb compiler.
#
# One front end rather than one executable per asset type; the input's extension already says what
# it is. Links CNA for the CNB codecs and the shared image decoder only -- it constructs no
# GraphicsDevice and opens no audio device, so it runs on a build machine with no display, no GPU
# and no sound card.
add_executable(cna_tool_source_to_cnb
    tools/source_to_cnb/source_to_cnb.cpp
)
target_include_directories(cna_tool_source_to_cnb PRIVATE
    # plans/plan_cnb.md CNBF-120: the shared strict command-line numeric parsers, reachable by
    # their bare header name from every content tool that takes a number.
    ${CNA_SOURCE_DIR}/tools/common
)
target_link_libraries(cna_tool_source_to_cnb
    PRIVATE
    CNA
)
cna_link_sharp_runtime(cna_tool_source_to_cnb PRIVATE)
