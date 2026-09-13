# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-310: `git diff --check` over the paths this plan owns.
#
# Run as a script rather than as a command line because the check needs the empty tree's hash,
# which has to be computed before the diff, and because `git diff --check` reports by exit status
# and by output alike.
execute_process(COMMAND "${GIT}" -C "${REPO}" hash-object -t tree /dev/null
                OUTPUT_VARIABLE _empty OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _status)
if(NOT _status EQUAL 0)
    message(FATAL_ERROR "XnaPipelineWhitespaceCheck: could not ask git for the empty tree")
endif()

execute_process(
    COMMAND "${GIT}" -C "${REPO}" diff --check "${_empty}" HEAD --
            modules/content modules/content-pipeline
            tools/xna-pipeline-oracle tools/xnb tools/content tools/provenance
            tests/reference/xna40 tests/assets/xna40 tests/assets/xna_custom_pipeline
            docs/xna-content-pipeline-parity-report.md docs/xma-encoder-backend.md
            plans/plan_xnapipeline_parity.md
    OUTPUT_VARIABLE _report ERROR_VARIABLE _errors RESULT_VARIABLE _status)
if(NOT _report STREQUAL "" OR NOT _status EQUAL 0)
    message(FATAL_ERROR
        "XnaPipelineWhitespaceCheck: whitespace errors in the paths this plan owns:\n${_report}${_errors}")
endif()
message(STATUS "XnaPipelineWhitespaceCheck: clean")
