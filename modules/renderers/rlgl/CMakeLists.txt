# plans/plan_rlgl.md RLGL-005: standalone rlgl is a header implementation. This target receives
# only rlgl's src/ include root and GRAPHICS_API_OPENGL_33; it does not link the raylib framework.
cna_add_renderer()
target_link_libraries(${RENDERER_TARGET} PRIVATE cna_rlgl_headers)

add_subdirectory(examples)
