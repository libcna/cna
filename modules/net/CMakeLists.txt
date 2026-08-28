file(GLOB_RECURSE CNA_NET_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(CNA_Net STATIC ${CNA_NET_SOURCES})
add_library(CNA::Net ALIAS CNA_Net)
target_link_libraries(CNA_Net PUBLIC cna_build_config)
target_include_directories(CNA_Net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(CNA_Net
    PUBLIC
    CNA_GamerServices
    enet
)
cna_link_sharp_runtime(CNA_Net PUBLIC Core.Base IO Collections.Core Runtime Threading)

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)
