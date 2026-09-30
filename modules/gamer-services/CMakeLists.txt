file(GLOB_RECURSE CNA_GAMERSERVICES_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(CNA_GamerServices STATIC ${CNA_GAMERSERVICES_SOURCES})
add_library(CNA::GamerServices ALIAS CNA_GamerServices)
target_link_libraries(CNA_GamerServices PUBLIC cna_build_config)
target_include_directories(CNA_GamerServices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The public dependency is the runtime layer (Guide/SignedInGamer use Game, graphics,
# input, audio through it) plus Storage (device selector flows).
target_link_libraries(CNA_GamerServices PUBLIC cna_runtime cna_storage)
cna_link_sharp_runtime(CNA_GamerServices PUBLIC Core.Base IO Collections.Core Globalization Runtime Threading Security.Cryptography TimeZone)

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)

# GS-003: control traffic uses mature TLS rather than sharp-runtime's HTTP-only transport.
if(NOT EMSCRIPTEN)
    find_package(CURL 7.85 REQUIRED)
    target_link_libraries(CNA_GamerServices PRIVATE CURL::libcurl)
endif()
# GS-009/GSP-B1: every original avatar catalog version (tools/avatar_builder) is compiled in as
# "v<N>/<file>", so standard avatars render offline and each description resolves against exactly
# the catalog version it names.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/EmbedBinaryFiles.cmake")
file(GLOB CNA_AVATAR_CATALOG_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars/v*/*")
cna_embed_binary_files(CNA_GamerServices CNA::Internal::GamerServices::Avatars embeddedCatalogFiles
    "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedAvatarCatalog.cpp" BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars"
    FILES ${CNA_AVATAR_CATALOG_FILES})
# The checked-in catalog must be exactly what the generator produces (tools/avatar_builder).
if(CNA_BUILD_TESTS)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME GamerServices_AvatarCatalogUpToDate
            COMMAND "${Python3_EXECUTABLE}" "${CNA_SOURCE_DIR}/tools/avatar_builder/generate_avatar_catalog.py" --check)
    endif()
endif()
# The avatar asset reader parses GLB with the cgltf implementation compiled into cna_content.
target_include_directories(CNA_GamerServices PRIVATE ${CNA_SOURCE_DIR}/third_party/cgltf)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HeaderOnlyJson.cmake")
cna_link_json_headers(CNA_GamerServices)
target_include_directories(CNA_GamerServices PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/Internal/Protocol)
