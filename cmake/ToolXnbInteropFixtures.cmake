# plans/plan_xnapipeline.md XNAP-30/XNAP-31: writes the CNA-generated XNB corpus and each
# fixture's expected-value manifest.
#
# Deliberately a tool rather than a test helper: the corpus is committed so that an XNA-capable
# Windows machine -- which this repository's CI does not have -- can be pointed at it directly,
# and a test then regenerates it to prove the committed bytes have not drifted.
add_executable(cna_tool_xnb_interop_fixtures
    tools/xnb/generate_interop_fixtures.cpp
)
target_link_libraries(cna_tool_xnb_interop_fixtures
    PRIVATE
    CNA
)
cna_link_sharp_runtime(cna_tool_xnb_interop_fixtures PRIVATE)
