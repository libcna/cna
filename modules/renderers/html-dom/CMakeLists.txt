# The browser platform owns canvas sizing, input and events; this renderer consumes its surface
# snapshot while every draw goes through EM_JS/DOM/CSS.
cna_add_renderer()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
