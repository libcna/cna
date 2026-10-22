# SPDX-License-Identifier: MS-PL
# Include local symbols: the old adapter hid its second runtime rather than
# exporting it, so an export-only audit could not detect the duplicated state.
execute_process(COMMAND "${CNA_NM}" -C --defined-only "${CNA_C_API_LIBRARY}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _symbols ERROR_VARIABLE _error)
if(NOT _result EQUAL 0 OR _symbols STREQUAL "")
    message(FATAL_ERROR "Cannot inspect the C adapter's runtime ownership: ${_error}")
endif()
string(REGEX MATCHALL
    "[0-9a-fA-F]+ [TtBbDd] (CNA::(Logger::|GetAssemblyTitleEXT|SetAssemblyTitleEXT)|Microsoft::Xna::Framework::Graphics::GraphicsAdapter::adapters_)[^\n]*"
    _duplicates "${_symbols}")
if(_duplicates)
    message(FATAL_ERROR "The C adapter contains private runtime definitions: ${_duplicates}")
endif()
execute_process(COMMAND "${CNA_READELF}" -d "${CNA_C_API_LIBRARY}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _dynamic ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect the C adapter's runtime dependency: ${_error}")
endif()
if(NOT _dynamic MATCHES "\\(NEEDED\\)[^\n]*\\[libcna\\.so\\]")
    message(FATAL_ERROR "The C adapter has no shared CNA runtime dependency")
endif()
message(STATUS "C adapter uses libcna.so without private logger, assembly, or adapter-registry definitions")
