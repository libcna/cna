include_guard(GLOBAL)

# Collapses the add_test() + set_tests_properties() pair repeated at every one of CNA's
# ~600 per-backend CTest registrations into a single call. Each backend keeps its own
# cna_<backend>_test(target src) macro for add_executable()+target_link_libraries() --
# linking differs enough per backend (Wine/DXVK wrapping, -Wl,--start-group circular-
# dependency links, extra libs) that unifying it isn't worth the risk -- this only
# unifies the registration half, which is identical logic across every backend.
#
# cna_register_backend_test(NAME <ctest-name> COMMAND <command...>
#                            [TIMEOUT <seconds>] [LABELS <label...>]
#                            [ENVIRONMENT <NAME=value;...>] [WORKING_DIRECTORY <dir>])
function(cna_register_backend_test)
    cmake_parse_arguments(T "" "NAME;TIMEOUT;WORKING_DIRECTORY" "COMMAND;LABELS;ENVIRONMENT" ${ARGN})

    add_test(NAME ${T_NAME} COMMAND ${T_COMMAND})

    set(_cna_test_props "")
    if(DEFINED T_TIMEOUT)
        list(APPEND _cna_test_props TIMEOUT ${T_TIMEOUT})
    endif()
    if(T_LABELS)
        list(APPEND _cna_test_props LABELS "${T_LABELS}")
    endif()
    if(T_ENVIRONMENT)
        list(APPEND _cna_test_props ENVIRONMENT "${T_ENVIRONMENT}")
    endif()
    if(DEFINED T_WORKING_DIRECTORY)
        list(APPEND _cna_test_props WORKING_DIRECTORY "${T_WORKING_DIRECTORY}")
    endif()
    if(_cna_test_props)
        set_tests_properties(${T_NAME} PROPERTIES ${_cna_test_props})
    endif()
endfunction()
