# SPDX-License-Identifier: MS-PL

if(NOT DEFINED CNA_C_API_INPUT_HEADER OR NOT DEFINED CNA_NATIVE_BUTTONS_HEADER)
    message(FATAL_ERROR "CNA gamepad button identity audit requires both header paths")
endif()

file(STRINGS "${CNA_NATIVE_BUTTONS_HEADER}" native_button_lines
    REGEX "^[ \t]*[A-Za-z0-9]+[ \t]*=[ \t]*0x[0-9A-Fa-f]+,?$")
file(STRINGS "${CNA_C_API_INPUT_HEADER}" c_button_lines
    REGEX "^#define CNA_GAMEPAD_BUTTON_[A-Z0-9_]+ UINT32_C\\(0x[0-9A-Fa-f]+\\)$")
list(FILTER c_button_lines EXCLUDE REGEX "CNA_GAMEPAD_BUTTON_(NONE|ALL)")

list(LENGTH native_button_lines native_button_count)
list(LENGTH c_button_lines c_button_count)
if(NOT native_button_count EQUAL 31 OR NOT c_button_count EQUAL native_button_count)
    message(FATAL_ERROR
        "CNA gamepad button identity count mismatch: "
        "native=${native_button_count}, C=${c_button_count}")
endif()

foreach(native_line IN LISTS native_button_lines)
    string(REGEX REPLACE
        "^[ \t]*([A-Za-z0-9]+)[ \t]*=[ \t]*(0x[0-9A-Fa-f]+),?$"
        "\\1;\\2"
        native_parts
        "${native_line}")
    list(GET native_parts 0 native_name)
    list(GET native_parts 1 native_value)
    string(REGEX REPLACE "([a-z0-9])([A-Z])" "\\1_\\2" c_name "${native_name}")
    string(TOUPPER "${c_name}" c_name)
    string(REPLACE "D_PAD" "DPAD" c_name "${c_name}")
    set(expected_line
        "#define CNA_GAMEPAD_BUTTON_${c_name} UINT32_C(${native_value})")
    list(FIND c_button_lines "${expected_line}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR
            "Missing or mismatched C gamepad button identity for "
            "${native_name}=${native_value}: expected '${expected_line}'")
    endif()
endforeach()
