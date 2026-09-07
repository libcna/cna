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
                 --plan "${CMAKE_CURRENT_SOURCE_DIR}/plans/plan_xnapipeline_parity.md"
                 --check)

add_test(NAME XnaPipelineInputParityMatrixIsCurrent
         COMMAND "${Python3_EXECUTABLE}" "${_xnapp_oracle}/inputs_matrix.py" check
                 --inventory "${_xnapp_reference}/content-pipeline-api.json"
                 --matrix "${_xnapp_reference}/content-pipeline-inputs.json"
                 --repo "${CMAKE_CURRENT_SOURCE_DIR}")

set_tests_properties(XnaPipelineParityReportIsCurrent XnaPipelineInputParityMatrixIsCurrent
                     PROPERTIES LABELS "parity;xnapipeline")

# =====================================================================================
# The rest of the plan's §29 gates, as ctests (plans/plan_xnapipeline_parity.md XNAPP-310).
#
# `--check` above answers "is the committed report a regeneration?". This one answers the other
# question -- "is the parity itself complete?" -- and fails on any MISSING type, member or enum
# value, on a status without its required note, and on a map entry the inventory does not have. It
# was run by hand while MISSING was still non-zero; it is zero now, so it is a test.
# =====================================================================================
add_test(NAME XnaPipelineParityGateIsGreen
         COMMAND "${Python3_EXECUTABLE}" "${_xnapp_oracle}/parity_report.py"
                 --inventory "${_xnapp_reference}/content-pipeline-api.json"
                 --map "${_xnapp_reference}/content-pipeline-parity-map.json"
                 --inputs "${_xnapp_reference}/content-pipeline-inputs.json"
                 --output "${CMAKE_CURRENT_SOURCE_DIR}/docs/xna-content-pipeline-parity-report.md"
                 --gate)

# The denominator is frozen at the measurement it was read from (XNAPP-016). Three things are
# compared and each fails for a different reason: the assemblies by digest and MVID, the counts, and
# the inventory file itself -- which may change only by recording the new digest as an event with a
# date and a reason.
add_test(NAME XnaPipelineInventoryIsFrozen
         COMMAND "${Python3_EXECUTABLE}" "${_xnapp_oracle}/inventory_freeze.py" check
                 --inventory "${_xnapp_reference}/content-pipeline-api.json"
                 --freeze "${_xnapp_reference}/content-pipeline-api.freeze.json")
set_tests_properties(XnaPipelineInventoryIsFrozen PROPERTIES LABELS "parity;xnapipeline")

# Every processor property's default, asserted in C++ against the value Microsoft's own class
# answered, with the assertions tied back to that measurement (XNAPP-023). A default is the most
# quietly wrong thing a reimplementation can have: nothing fails, the content is just different.
add_test(NAME XnaPipelineProcessorDefaultsMatchTheMeasurement
         COMMAND "${Python3_EXECUTABLE}" "${_xnapp_oracle}/processor_defaults_gate.py"
                 --inventory "${_xnapp_reference}/content-pipeline-api.json"
                 --test "${CMAKE_CURRENT_SOURCE_DIR}/modules/content-pipeline/tests/Microsoft/Xna/Framework/Content/Pipeline/Processors/XnaProcessorDefaultsTests.cpp")
set_tests_properties(XnaPipelineProcessorDefaultsMatchTheMeasurement
                     PROPERTIES LABELS "parity;xnapipeline")

# The provenance gate: nothing Microsoft owns is in the tree, no font that is not licensed for it,
# no production source carrying someone else's copyright header, every vendored third party
# declared, and the directory the oracle copies Microsoft's assemblies into really is ignored.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/provenance/provenance_gate.py")
    add_test(NAME CnaProvenanceGate
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tools/provenance/provenance_gate.py"
                     --repo "${CMAKE_CURRENT_SOURCE_DIR}")
    # A gate that has never failed is a gate nobody has tested. This plants one of each thing the
    # gate exists to find in a throwaway repository and checks that it finds exactly that, so the
    # green run above means the tree is clean rather than that the gate is asleep.
    add_test(NAME CnaProvenanceGateSelfTest
             COMMAND "${Python3_EXECUTABLE}"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tools/provenance/provenance_gate_selftest.py"
                     --gate "${CMAKE_CURRENT_SOURCE_DIR}/tools/provenance/provenance_gate.py")
    set_tests_properties(CnaProvenanceGate CnaProvenanceGateSelfTest
                         PROPERTIES LABELS "parity;xnapipeline;provenance")
endif()

# `git diff --check` over everything this plan owns, against the empty tree, so it reads the
# committed content rather than only an uncommitted change. Deliberately scoped: the repository has
# thousands of whitespace complaints in prose written long before this plan, and a gate that can
# never be green is a gate somebody turns off.
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    add_test(NAME XnaPipelineWhitespaceIsClean
             COMMAND "${CMAKE_COMMAND}"
                     "-DGIT=${GIT_EXECUTABLE}" "-DREPO=${CMAKE_CURRENT_SOURCE_DIR}"
                     -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/XnaPipelineWhitespaceCheck.cmake")
    set_tests_properties(XnaPipelineWhitespaceIsClean
                         PROPERTIES LABELS "parity;xnapipeline")
endif()

# The genuine-runtime harness. It needs mono, Wine, a display and a legally installed XNA 4.0, and
# says so by exiting 3, which ctest reports as a skip rather than a failure -- the difference
# between "this machine cannot answer" and "the answer was wrong".
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/xna40/run-interop-harness.sh")
    add_test(NAME XnaPipelineGenuineRuntimeInterop
             COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/xna40/run-interop-harness.sh")
    set_tests_properties(XnaPipelineGenuineRuntimeInterop
                         PROPERTIES LABELS "parity;xnapipeline;interop"
                                    SKIP_RETURN_CODE 3 TIMEOUT 900)
endif()

# The three output families whose fixtures cannot be committed with the rest -- Effect, Song and
# Video -- built from CNA's own sources and loaded in the same genuine runtime (XNAPP-281). Skips
# the same way when the machine cannot answer.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/xna40/run-built-families-interop.sh")
    add_test(NAME XnaPipelineGenuineRuntimeBuiltFamilies
             COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/xna40/run-built-families-interop.sh")
    set_tests_properties(XnaPipelineGenuineRuntimeBuiltFamilies
                         PROPERTIES LABELS "parity;xnapipeline;interop"
                                    SKIP_RETURN_CODE 3 TIMEOUT 900)
endif()

# And the LZX half of the committed corpus, which is the same assets through the compressor XNA
# itself produced.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/xna40/run-interop-harness.sh")
    add_test(NAME XnaPipelineGenuineRuntimeInteropLzx
             COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/xna40/run-interop-harness.sh"
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/xnb/cna/windows/lzx")
    set_tests_properties(XnaPipelineGenuineRuntimeInteropLzx
                         PROPERTIES LABELS "parity;xnapipeline;interop"
                                    SKIP_RETURN_CODE 3 TIMEOUT 900)
endif()

set_tests_properties(XnaPipelineParityGateIsGreen PROPERTIES LABELS "parity;xnapipeline")
