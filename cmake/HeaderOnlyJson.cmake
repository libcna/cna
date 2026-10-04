# SPDX-License-Identifier: MS-PL
include_guard(GLOBAL)

# Cross toolchains can consume the host's portable JSON headers, but never its libc include root --
# and commonly confine package lookup to their target sysroot, where none is installed.
function(cna_link_json_headers target)
    if(CMAKE_CROSSCOMPILING)
        if(NOT TARGET CNA_JsonHeaders)
            find_package(nlohmann_json 3.11 REQUIRED NO_CMAKE_FIND_ROOT_PATH)
            get_target_property(_json_roots nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
            set(_json_include "${CMAKE_CURRENT_BINARY_DIR}/nlohmann-json-headers/include")
            file(MAKE_DIRECTORY "${_json_include}")
            set(_json_found FALSE)
            foreach(_json_root IN LISTS _json_roots)
                if(EXISTS "${_json_root}/nlohmann/json.hpp")
                    file(CREATE_LINK "${_json_root}/nlohmann" "${_json_include}/nlohmann" SYMBOLIC)
                    set(_json_found TRUE)
                    break()
                endif()
            endforeach()
            if(NOT _json_found)
                message(FATAL_ERROR "nlohmann_json package does not expose its header directory")
            endif()
            add_library(CNA_JsonHeaders INTERFACE IMPORTED GLOBAL)
            set_target_properties(CNA_JsonHeaders PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${_json_include}"
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_json_include}")
        endif()
        target_link_libraries(${target} PRIVATE CNA_JsonHeaders)
    else()
        find_package(nlohmann_json 3.11 REQUIRED)
        target_link_libraries(${target} PRIVATE nlohmann_json::nlohmann_json)
    endif()
endfunction()
