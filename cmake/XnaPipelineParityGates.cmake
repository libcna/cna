# =====================================================================================
# XNA 4.0 Content Pipeline parity gates (plans/plan_xnapipeline_parity.md XNAPP-015, XNAPP-021)
#
# Two things these gates exist to stop, both of which happened before they did.
#
# The first is a status that drifts without the report saying so: the coverage report is a
# generated artefact, and `parity_report.py --check` fails unless the committed file is byte for
# byte what a regeneration writes. A map edited without regenerating, or a note removed from a
# status that requires one, fails here rather than in a later reading of the document.
#
# The second is a source extension that leaves the denominator because nobody listed it. The
# eighteen extensions are read from the genuine assemblies' importer attributes, and
# `inputs_matrix.py check` fails if the matrix has fewer, if it names one the assemblies do not,
# if a status is outside the vocabulary, or if a test or fixture it names does not exist in the
# tree. A percentage nobody can compute by hand is the point.
#
# Neither gate fails on MISSING. MISSING is what the report publishes; the completion gate is
# `parity_report.py --gate`, run by hand while MISSING is still non-zero.
# =====================================================================================
if(NOT CNA_BUILD_TESTS)
    return()
endif()

if(NOT DEFINED Python3_Interpreter_FOUND)
    find_package(Python3 QUIET COMPONENTS Interpreter)
endif()

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "CNA: XNA pipeline parity gates skipped (no Python 3 interpreter found)")
    return()
endif()

set(_xnapp_oracle "${CMAKE_CURRENT_SOURCE_DIR}/tools/xna-pipeline-oracle")
set(_xnapp_reference "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference/xna40")

if(NOT EXISTS "${_xnapp_oracle}/parity_report.py")
    message(STATUS "CNA: XNA pipeline parity gates skipped (the oracle is not in this tree)")
    return()
endif()

add_test(NAME XnaPipelineParityReportIsCurrent
         COMMAND "${Python3_EXECUTABLE}" "${_xnapp_oracle}/parity_report.py"
                 --inventory "${_xnapp_reference}/content-pipeline-api.json"
                 --map "${_xnapp_reference}/content-pipeline-parity-map.json"
                 --inputs "${_xnapp_reference}/content-pipeline-inputs.json"
                 --output "${CMAKE_CURRENT_SOURCE_DIR}/docs/xna-content-pipeline-parity-report.md"
                 --check)

add_test(NAME XnaPipelineInputParityMatrixIsCurrent
         COMMAND "${Python3_EXECUTABLE}" "${_xnapp_oracle}/inputs_matrix.py" check
                 --inventory "${_xnapp_reference}/content-pipeline-api.json"
                 --matrix "${_xnapp_reference}/content-pipeline-inputs.json"
                 --repo "${CMAKE_CURRENT_SOURCE_DIR}")

set_tests_properties(XnaPipelineParityReportIsCurrent XnaPipelineInputParityMatrixIsCurrent
                     PROPERTIES LABELS "parity;xnapipeline")
