# SPDX-License-Identifier: MS-PL
include_guard(GLOBAL)

option(CNA_CONFIGURE_AUDIT_CACHE
    "Cache successful configure-time audits by a content fingerprint of their inputs" ON)

function(cna_run_configure_audit)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "NAME;RESULT;OUTPUT;ERROR"
        "SHARED_INPUTS;INPUTS;COMMAND")
    if(NOT ARG_NAME OR NOT ARG_RESULT OR NOT ARG_OUTPUT OR NOT ARG_ERROR OR NOT ARG_COMMAND)
        message(FATAL_ERROR
            "cna_run_configure_audit requires NAME, RESULT, OUTPUT, ERROR, and COMMAND")
    endif()

    set(_audit_cache_helper "${CNA_SOURCE_DIR}/tools/build/configure_audit_cache.py")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_audit_cache_helper}")
    if(CNA_CONFIGURE_AUDIT_CACHE AND EXISTS "${_audit_cache_helper}")
        set(_audit_seed_arguments)
        if(ARG_SHARED_INPUTS)
            string(SHA256 _audit_shared_property_suffix "${ARG_SHARED_INPUTS}")
            set(_audit_shared_property
                "CNA_CONFIGURE_AUDIT_DIGEST_${_audit_shared_property_suffix}")
            get_property(_audit_shared_digest GLOBAL PROPERTY "${_audit_shared_property}")
            if(NOT _audit_shared_digest)
                set(_audit_shared_input_arguments)
                foreach(_audit_shared_input IN LISTS ARG_SHARED_INPUTS)
                    list(APPEND _audit_shared_input_arguments --input "${_audit_shared_input}")
                endforeach()
                execute_process(
                    COMMAND "${Python3_EXECUTABLE}" "${_audit_cache_helper}"
                            --name shared-inputs
                            --repo "${CNA_SOURCE_DIR}"
                            --no-cache
                            --digest-only
                            ${_audit_shared_input_arguments}
                    RESULT_VARIABLE _audit_shared_result
                    OUTPUT_VARIABLE _audit_shared_digest
                    ERROR_VARIABLE _audit_shared_error
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_STRIP_TRAILING_WHITESPACE)
                if(NOT _audit_shared_result EQUAL 0)
                    message(FATAL_ERROR
                        "CNA configure-audit input fingerprint failed:\n${_audit_shared_error}")
                endif()
                set_property(GLOBAL PROPERTY "${_audit_shared_property}"
                    "${_audit_shared_digest}")
            endif()
            list(APPEND _audit_seed_arguments --seed "${_audit_shared_digest}")
        endif()
        set(_audit_input_arguments)
        foreach(_audit_input IN LISTS ARG_INPUTS)
            list(APPEND _audit_input_arguments --input "${_audit_input}")
        endforeach()
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${_audit_cache_helper}"
                    --name "${ARG_NAME}"
                    --repo "${CNA_SOURCE_DIR}"
                    --cache "${CMAKE_BINARY_DIR}/CMakeFiles/cna-configure-audit-${ARG_NAME}.json"
                    ${_audit_seed_arguments}
                    ${_audit_input_arguments}
                    -- ${ARG_COMMAND}
            WORKING_DIRECTORY "${CNA_SOURCE_DIR}"
            RESULT_VARIABLE _audit_result
            OUTPUT_VARIABLE _audit_output
            ERROR_VARIABLE _audit_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE)
    else()
        execute_process(
            COMMAND ${ARG_COMMAND}
            WORKING_DIRECTORY "${CNA_SOURCE_DIR}"
            RESULT_VARIABLE _audit_result
            OUTPUT_VARIABLE _audit_output
            ERROR_VARIABLE _audit_error
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE)
    endif()

    set(${ARG_RESULT} "${_audit_result}" PARENT_SCOPE)
    set(${ARG_OUTPUT} "${_audit_output}" PARENT_SCOPE)
    set(${ARG_ERROR} "${_audit_error}" PARENT_SCOPE)
endfunction()
