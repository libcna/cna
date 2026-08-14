# SPDX-License-Identifier: MS-PL

if(NOT DEFINED CNA_C_API_INCLUDE_DIRECTORY)
    message(FATAL_ERROR "CNA_C_API_INCLUDE_DIRECTORY is required.")
endif()

file(GLOB_RECURSE _cna_c_api_headers LIST_DIRECTORIES FALSE
    "${CNA_C_API_INCLUDE_DIRECTORY}/CNA/C/*.h")
if(NOT _cna_c_api_headers)
    message(FATAL_ERROR "No public CNA C API headers were found.")
endif()

foreach(_cna_c_api_header IN LISTS _cna_c_api_headers)
    file(READ "${_cna_c_api_header}" _cna_c_api_source)

    # Remove comments before lexical checks. The headers deliberately do not nest comments.
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" _cna_c_api_source "${_cna_c_api_source}")
    string(REGEX REPLACE "//[^\n]*" "" _cna_c_api_source "${_cna_c_api_source}")

    string(REGEX MATCH
        "System::|SharpRuntime|std::|(^|[^A-Za-z0-9_])namespace([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])class([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])template([^A-Za-z0-9_]|$)|(^|[^A-Za-z0-9_])throw([^A-Za-z0-9_]|$)"
        _cna_c_api_forbidden_token
        "${_cna_c_api_source}")
    if(_cna_c_api_forbidden_token)
        message(FATAL_ERROR
            "C API public header ${_cna_c_api_header} contains forbidden C++/Sharp Runtime token: "
            "${_cna_c_api_forbidden_token}")
    endif()

    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]"
        _cna_c_api_includes "${_cna_c_api_source}")
    foreach(_cna_c_api_include IN LISTS _cna_c_api_includes)
        string(REGEX REPLACE ".*[<\"]([^>\"]+)[>\"].*" "\\1"
            _cna_c_api_include_name "${_cna_c_api_include}")
        if(NOT _cna_c_api_include_name MATCHES
               [[^(stdint\.h|stddef\.h|stdbool\.h|CNA/C/[A-Za-z0-9_]+\.h)$]])
            message(FATAL_ERROR
                "C API public header ${_cna_c_api_header} includes unsupported header "
                "${_cna_c_api_include_name}.")
        endif()
    endforeach()
endforeach()
