file(GLOB_RECURSE CNA_GAMERSERVICES_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(CNA_GamerServices STATIC ${CNA_GAMERSERVICES_SOURCES})
add_library(CNA::GamerServices ALIAS CNA_GamerServices)
target_link_libraries(CNA_GamerServices PUBLIC cna_build_config)
target_include_directories(CNA_GamerServices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# A released avatar description must assemble the same model on every machine; the digests in
# AvatarCatalogTest.V3DescriptionsAssembleExactlyAsReleased pin it. Clang 14+ and GCC contract
# `a * b + c` into a fused multiply-add wherever the target has one, which every arm64 machine
# does, and on Apple silicon that moved assembled vertices across the digest's quantization step.
# An x86-64 build without -mfma has no FMA to contract into, so this changes nothing there.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/Internal/Avatars/AvatarModel.cpp"
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()
# The public dependency is the runtime layer (Guide/SignedInGamer use Game, graphics,
# input, audio through it) plus Storage (device selector flows).
target_link_libraries(CNA_GamerServices PUBLIC cna_runtime cna_storage)
cna_link_sharp_runtime(CNA_GamerServices PUBLIC Core.Base IO Collections.Core Globalization Runtime Threading Security.Cryptography TimeZone)

# Module-owned examples (module-examples campaign): demo/example registration is
# module-local; the conditions historically applied by cmake/Examples.cmake live
# inside examples/CMakeLists.txt unchanged.
add_subdirectory(examples)

# GS-003: control traffic uses mature TLS rather than sharp-runtime's HTTP-only transport. The
# Android NDK ships no libcurl, so there it is linked only when one is found; without it the
# service refuses (SERVICE_TRANSPORT_UNAVAILABLE), as it does in a browser. Desktop builds still
# require it.
if(NOT EMSCRIPTEN)
    if(ANDROID)
        find_package(CURL 7.85)
    else()
        find_package(CURL 7.85 REQUIRED)
    endif()
    if(CURL_FOUND)
        target_link_libraries(CNA_GamerServices PRIVATE CURL::libcurl)
        target_compile_definitions(CNA_GamerServices PRIVATE CNA_SERVICE_TLS_TRANSPORT)
    else()
        message(STATUS "CNA: no libcurl for ${CMAKE_SYSTEM_NAME}; the CNA service transport refuses (SERVICE_TRANSPORT_UNAVAILABLE)")
    endif()
endif()
# The Windows credential store seals the refresh token with DPAPI (CryptProtectData), which lives
# in crypt32 -- a library neither MinGW-w64 nor MSVC links by default.
if(WIN32)
    target_link_libraries(CNA_GamerServices PRIVATE crypt32)
endif()
# GS-009/GSP-B1: the avatar catalogs this release ships (tools/avatar_builder) are compiled in as
# "v<N>/<file>", so standard avatars render offline and each description resolves against exactly
# the catalog version it names. Catalog v3 is the baseline: the oldest version CNA supports
# (Avatars::BaseCatalogVersion), and today the only one. Catalogs 1 and 2 were development
# catalogs and are not supported. A future catalog is added by generating its directory and
# appending its version here; every catalog in the list stays compiled in once released.
#
# Link boundary (docs/link-size-architecture.md): the catalog is ~30 MB of GLB and PNG data,
# compiled in deflated (about a third of that; inflated file by file as an avatar needs it), so it
# is a library of its own whose one symbol, embeddedCatalogFiles(), only the avatar catalog code
# (src/Internal/Avatars/AvatarCatalog.cpp) references. A program reaches it by drawing or editing
# avatars -- AvatarDescription, AvatarRenderer, or the system Guide GamerServicesDispatcher installs
# -- never through Guide.IsTrialMode, a message box or the service client.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/EmbedBinaryFiles.cmake")
set(CNA_AVATAR_CATALOG_VERSIONS 3)
set(CNA_AVATAR_CATALOG_FILES)
foreach(_version IN LISTS CNA_AVATAR_CATALOG_VERSIONS)
    set(_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars/v${_version}")
    if(NOT EXISTS "${_directory}/catalog.json")
        message(FATAL_ERROR "CNA: avatar catalog v${_version} is listed in CNA_AVATAR_CATALOG_VERSIONS but ${_directory}/catalog.json is missing")
    endif()
    file(GLOB _files CONFIGURE_DEPENDS "${_directory}/*")
    list(APPEND CNA_AVATAR_CATALOG_FILES ${_files})
endforeach()
# A catalog directory outside the list would be checked in but never shipped.
file(GLOB _catalog_directories LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars"
    CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars/*")
foreach(_entry IN LISTS _catalog_directories)
    string(REGEX REPLACE "^v" "" _version "${_entry}")
    if(NOT _version IN_LIST CNA_AVATAR_CATALOG_VERSIONS)
        message(FATAL_ERROR "CNA: assets/avatars/${_entry} is not a catalog this release ships (CNA_AVATAR_CATALOG_VERSIONS: ${CNA_AVATAR_CATALOG_VERSIONS})")
    endif()
endforeach()
add_library(cna_gamerservices_avatar_catalog STATIC)
add_library(CNA::GamerServicesAvatarCatalog ALIAS cna_gamerservices_avatar_catalog)
cna_embed_binary_files(cna_gamerservices_avatar_catalog CNA::Internal::GamerServices::Avatars embeddedCatalogFiles
    "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedAvatarCatalog.cpp" BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/avatars"
    COMPRESS FILES ${CNA_AVATAR_CATALOG_FILES})
# Debug information would repeat every byte of the arrays (GCC describes a constant array with its
# value), doubling what every debug program that draws avatars links.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedAvatarCatalog.cpp" PROPERTIES COMPILE_OPTIONS "-g0")
endif()
target_link_libraries(cna_gamerservices_avatar_catalog PRIVATE cna_build_config cna_core_headers)
target_link_libraries(CNA_GamerServices PRIVATE cna_gamerservices_avatar_catalog)
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
# The compiled-in catalog is deflated; stb_image's zlib decoder inflates it (AvatarInflate.cpp).
target_include_directories(CNA_GamerServices PRIVATE ${CNA_SOURCE_DIR}/third_party/stb)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HeaderOnlyJson.cmake")
cna_link_json_headers(CNA_GamerServices)
target_include_directories(CNA_GamerServices PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/Internal/Protocol)
