/* SPDX-License-Identifier: MS-PL */

/*
 * plans/plan_binding.md CBIND-113 -- so a failing C API suite says which stage failed.
 *
 * `CBIND-101` closed on fifteen serial runs that produced a single occurrence, and the complete
 * capture of that occurrence was the renderer banner and nothing else: the suite reported only
 * through its exit code, so `--output-on-failure` could say no more than "it failed". Placing an
 * intermittent failure then costs a rerun that may not reproduce it.
 *
 * The audit behind this header measured all 83 pure-C suites rather than assuming: 1 is a
 * compile-time `_Static_assert` wall where the compiler already names the line, 29 printed
 * something identifying the failure, 38 identified their stage by **exit code alone**, and 15 said
 * nothing at all. The 53 in those last two groups signal failure at 298 sites, in four shapes --
 * which is why this is two small macros used at those sites rather than a rewrite of the suites.
 *
 * Neither macro changes what a suite does. `CNA_TEST_FAIL` evaluates to the code it is given, so
 * `return CNA_TEST_FAIL(4);` still returns 4 and `status = CNA_TEST_FAIL(7);` still sets 7;
 * `CNA_TEST_STAGE` evaluates to what the call returned, so `&&` and `||` still short-circuit
 * exactly as before.
 */

#ifndef CNA_C_API_TEST_REPORT_H
#define CNA_C_API_TEST_REPORT_H

#include <stdio.h>

/**
 * @brief Reports a failing exit code with its source location, and evaluates to that code.
 *
 * @param file Source file, from `__FILE__`.
 * @param line Source line, from `__LINE__`.
 * @param function Enclosing function, from `__func__` -- which is what names the stage.
 * @param code The exit code this failure produces.
 * @return @p code, unchanged.
 */
static inline int cna_test_report_failure(
    const char* const file,
    const int line,
    const char* const function,
    const int code)
{
    (void)fprintf(stderr, "%s:%d: %s: FAILED, exit code %d\n", file, line, function, code);
    (void)fflush(stderr);
    return code;
}

/**
 * @brief Reports a failing stage with its source location, and evaluates to what the stage returned.
 *
 * @param file Source file, from `__FILE__`.
 * @param line Source line, from `__LINE__`.
 * @param text The stage expression, stringified.
 * @param ok What the stage returned; nonzero is success.
 * @return @p ok, unchanged.
 */
static inline int cna_test_report_stage(
    const char* const file,
    const int line,
    const char* const text,
    const int ok)
{
    if (!ok) {
        (void)fprintf(stderr, "%s:%d: FAILED: %s\n", file, line, text);
        (void)fflush(stderr);
    }
    return ok;
}

/** @brief Wraps a failing exit code so it names its own file, line and enclosing function. */
#define CNA_TEST_FAIL(code) cna_test_report_failure(__FILE__, __LINE__, __func__, (code))

/** @brief Wraps a stage call so a failure names its own file, line and expression. */
#define CNA_TEST_STAGE(call) cna_test_report_stage(__FILE__, __LINE__, #call, (call))

#endif /* CNA_C_API_TEST_REPORT_H */
