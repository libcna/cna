# SPDX-License-Identifier: MS-PL

if(NOT DEFINED CNA_C_API_WASM_MODULE OR NOT EXISTS "${CNA_C_API_WASM_MODULE}")
    message(FATAL_ERROR "The generated C API Wasm module was not provided.")
endif()
if(NOT DEFINED CNA_C_API_WASM_RENDERER)
    message(FATAL_ERROR "The C API Wasm renderer identity was not provided.")
endif()

file(READ "${CNA_C_API_WASM_MODULE}" _cna_wasm_javascript)

# Asyncify adds this runtime object to Emscripten's generated JavaScript. Its absence checks the
# actual linked artifact rather than trusting target option order (where a later transitive
# -sASYNCIFY=1 would override an earlier -sASYNCIFY=0).
if(_cna_wasm_javascript MATCHES "var[ \t]+Asyncify|Asyncify[ \t]*=[ \t]*\\{")
    message(FATAL_ERROR
        "cna_c_api_wasm contains the Asyncify runtime; RunOneFrame exports must be synchronous.")
endif()

if(CNA_C_API_WASM_RENDERER STREQUAL "WEBGL2")
    set(_cna_expected_webgl_version 2)
    if(NOT _cna_wasm_javascript MATCHES "majorVersion[ \t]*:[ \t]*2([^0-9]|$)")
        message(FATAL_ERROR
            "cna_c_api_wasm does not request WebGL 2 for ${CNA_C_API_WASM_RENDERER}.")
    endif()
    if(_cna_wasm_javascript MATCHES "majorVersion[ \t]*:[ \t]*1([^0-9]|$)")
        message(FATAL_ERROR
            "cna_c_api_wasm also contains a WebGL 1 context request; "
            "the renderer contract must be exact.")
    endif()
else()
    set(_cna_expected_webgl_version 0)
endif()

message(STATUS
    "cna_c_api_wasm link contract: renderer=${CNA_C_API_WASM_RENDERER}, "
    "WebGL=${_cna_expected_webgl_version}, Asyncify=OFF")
