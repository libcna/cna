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

# Canonical CNA service relay envelope is private to the realtime implementation.
target_include_directories(CNA_Net PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gamer-services/src/Internal/Protocol)

# Private welcome decoding uses the same bounded JSON parser as the service control backend.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HeaderOnlyJson.cmake")
cna_link_json_headers(CNA_Net)

# Mature secure WebSocket primitives are transport-only; XNA/control policy stays in CNA.
if(NOT EMSCRIPTEN)
    find_package(CURL 7.86 REQUIRED)
    target_link_libraries(CNA_Net PRIVATE CURL::libcurl)
endif()
