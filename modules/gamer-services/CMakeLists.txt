file(GLOB_RECURSE CNA_GAMERSERVICES_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(CNA_GamerServices STATIC ${CNA_GAMERSERVICES_SOURCES})
add_library(CNA::GamerServices ALIAS CNA_GamerServices)
target_link_libraries(CNA_GamerServices PUBLIC cna_build_flags)
target_include_directories(CNA_GamerServices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Guide.cpp calls SDL3 directly (message box); this was previously only compiling by
# accident on hosts with a stray system-wide SDL3 install on the default include path.
# The public dependency is the runtime layer (Guide/SignedInGamer use Game, graphics,
# input, audio through it) plus Storage (device selector flows).
target_link_libraries(CNA_GamerServices PUBLIC cna_runtime cna_storage PRIVATE SDL3::SDL3)
cna_link_sharp_runtime(CNA_GamerServices PUBLIC Core.Base IO Collections.Core Globalization Runtime Threading)
