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
#                            [ENVIRONMENT <NAME=value;...>] [WORKING_DIRECTORY <dir>]
#                            [SKIP_REGULAR_EXPRESSION <pattern>])
#
# ENVIRONMENT/LABELS values routinely contain literal semicolons (e.g.
# "SDL_VIDEODRIVER=x11;DISPLAY=:0", "GraphicsSmoke;WebGPU"). Those must be
# \;-escaped before going into the intermediate _cna_test_props list, otherwise
# list(APPEND) flattens them into extra list elements and the final
# set_tests_properties(... PROPERTIES ${_cna_test_props}) call receives a
# corrupted, unbalanced argument list.
function(cna_register_backend_test)
    cmake_parse_arguments(T "" "NAME;TIMEOUT;WORKING_DIRECTORY;SKIP_REGULAR_EXPRESSION" "COMMAND;LABELS;ENVIRONMENT" ${ARGN})

    add_test(NAME ${T_NAME} COMMAND ${T_COMMAND})

    set(_cna_test_props "")
    if(DEFINED T_TIMEOUT)
        list(APPEND _cna_test_props TIMEOUT ${T_TIMEOUT})
    endif()
    if(T_LABELS)
        string(REPLACE ";" "\\;" _cna_labels_escaped "${T_LABELS}")
        list(APPEND _cna_test_props LABELS "${_cna_labels_escaped}")
    endif()
    if(T_ENVIRONMENT)
        string(REPLACE ";" "\\;" _cna_environment_escaped "${T_ENVIRONMENT}")
        list(APPEND _cna_test_props ENVIRONMENT "${_cna_environment_escaped}")
    endif()
    if(DEFINED T_WORKING_DIRECTORY)
        list(APPEND _cna_test_props WORKING_DIRECTORY "${T_WORKING_DIRECTORY}")
    endif()
    if(T_SKIP_REGULAR_EXPRESSION)
        string(REPLACE ";" "\\;" _cna_skip_regex_escaped "${T_SKIP_REGULAR_EXPRESSION}")
        list(APPEND _cna_test_props SKIP_REGULAR_EXPRESSION "${_cna_skip_regex_escaped}")
    endif()
    if(_cna_test_props)
        set_tests_properties(${T_NAME} PROPERTIES ${_cna_test_props})
    endif()
endfunction()
