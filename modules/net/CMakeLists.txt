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

# Network voice encodes with Opus. AUTO carries voice when a native build has libopus
# (pkg-config); without it NetworkGamer.HasVoice stays false. Opus is not vendored. The plain
# pkg-config probe describes the host and therefore cannot be used for any cross build: doing so
# can compile successfully and then inject the host's `-lopus` into the target link command.
set(CNA_ENABLE_VOICE "AUTO" CACHE STRING "Network voice through libopus: AUTO, ON or OFF")
set_property(CACHE CNA_ENABLE_VOICE PROPERTY STRINGS AUTO ON OFF)
# Decided by this configure's probe, not by CNA_OPUS_FOUND alone: pkg_check_modules caches that, so
# a tree configured once with the probe would otherwise keep linking a target no longer defined.
set(_cna_voice_opus FALSE)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/HostPkgConfig.cmake")
cna_can_probe_host_pkg_config(_cna_voice_can_probe_host)
if(NOT CNA_ENABLE_VOICE STREQUAL "OFF" AND _cna_voice_can_probe_host)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(CNA_OPUS QUIET IMPORTED_TARGET opus>=1.3)
        if(CNA_OPUS_FOUND AND TARGET PkgConfig::CNA_OPUS)
            set(_cna_voice_opus TRUE)
        endif()
    endif()
endif()
if(_cna_voice_opus)
    target_link_libraries(CNA_Net PRIVATE PkgConfig::CNA_OPUS)
    target_compile_definitions(CNA_Net PRIVATE CNA_VOICE_OPUS=1)
    message(STATUS "CNA: network voice with Opus ${CNA_OPUS_VERSION}")
elseif(CNA_ENABLE_VOICE STREQUAL "ON")
    message(FATAL_ERROR "CNA_ENABLE_VOICE=ON needs libopus 1.3 or newer (Debian/Ubuntu: libopus-dev)")
else()
    message(STATUS "CNA: network voice off (libopus not found or CNA_ENABLE_VOICE=OFF)")
endif()

# Mature secure WebSocket primitives are transport-only; XNA/control policy stays in CNA. Optional on
# Android for the reason modules/gamer-services/CMakeLists.txt gives; the relay then refuses.
if(NOT EMSCRIPTEN)
    if(ANDROID)
        find_package(CURL 7.86)
    else()
        find_package(CURL 7.86 REQUIRED)
    endif()
    if(CURL_FOUND)
        target_link_libraries(CNA_Net PRIVATE CURL::libcurl)
        target_compile_definitions(CNA_Net PRIVATE CNA_SERVICE_TLS_TRANSPORT)
    endif()
endif()
