# SPDX-License-Identifier: MS-PL

if(NOT DEFINED CNA_C_API_INPUT_HEADER OR NOT DEFINED CNA_NATIVE_KEYS_HEADER)
    message(FATAL_ERROR "CNA key identity audit requires both header paths")
endif()

file(STRINGS "${CNA_NATIVE_KEYS_HEADER}" native_key_lines
    REGEX "^[ \t]*[A-Za-z0-9]+ = [0-9]+,?$")
file(STRINGS "${CNA_C_API_INPUT_HEADER}" c_key_lines
    REGEX "^#define CNA_KEY_[A-Z0-9_]+ UINT32_C\\([0-9]+\\)$")

list(LENGTH native_key_lines native_key_count)
list(LENGTH c_key_lines c_key_count)
if(NOT native_key_count EQUAL 160 OR NOT c_key_count EQUAL native_key_count)
    message(FATAL_ERROR
        "CNA key identity count mismatch: native=${native_key_count}, C=${c_key_count}")
endif()

foreach(native_line IN LISTS native_key_lines)
    string(REGEX REPLACE
        "^[ \t]*([A-Za-z0-9]+) = ([0-9]+),?$"
        "\\1;\\2"
        native_parts
        "${native_line}")
    list(GET native_parts 0 native_name)
    list(GET native_parts 1 native_value)
    string(REGEX REPLACE "([a-z0-9])([A-Z])" "\\1_\\2" c_name "${native_name}")
    string(TOUPPER "${c_name}" c_name)
    set(expected_line "#define CNA_KEY_${c_name} UINT32_C(${native_value})")
    list(FIND c_key_lines "${expected_line}" expected_index)
    if(expected_index EQUAL -1)
        message(FATAL_ERROR
            "Missing or mismatched C key identity for ${native_name}=${native_value}: "
            "expected '${expected_line}'")
    endif()
endforeach()
