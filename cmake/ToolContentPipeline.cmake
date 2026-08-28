# plans/plan_content_pipeline.md CP-006: unified CNA Content Pipeline front end.
add_executable(cna_content_tool
    tools/content/content.cpp
)
set_target_properties(cna_content_tool PROPERTIES OUTPUT_NAME "cna-content")
target_include_directories(cna_content_tool PRIVATE
    ${CNA_SOURCE_DIR}/tools/common
)
target_link_libraries(cna_content_tool PRIVATE CNA)
cna_link_sharp_runtime(cna_content_tool PRIVATE)
