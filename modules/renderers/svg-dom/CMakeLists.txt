# The browser platform owns canvas sizing, input and events. Every draw goes through EM_JS/real SVG
# DOM elements, and CNA's shared image encoder backs the data-URI texture path.
cna_add_renderer()

# Module-owned examples (module-examples campaign): the family's example/test
# registration is module-local; its historical selection/platform conditions
# live inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
