<!-- SPDX-License-Identifier: MS-PL -->
# XNA 4 runtime exception compatibility

SAMPLE-104 exposed the visible distinction between Framework 4 and modern .NET ArgumentException
messages. The unchanged Windows/x86 XNA console catches System.Exception and draws Message:
Framework uses a separate `Parameter name:` line, while modern .NET uses an inline suffix.

Sharp Runtime now supplies the general, off-by-default AppContext switch
`SharpRuntime.UseNetFrameworkArgumentExceptionMessages`. Game selects it at the XNA host boundary
only if the caller has not explicitly set a value. Standalone Sharp retains its modern default;
explicit false remains false. No sample knows about this setting, and no public XNA/C ABI or
class-layout change is involved in CNA. System.String.Substring also receives the shared correct
validation precedence, parameter and overflow-safe bounds checking.

Two runtime regressions exercise default XNA behavior and explicit modern override. The full
OPENGLES3 CnaRuntimeTests binary is run through `tools/platform/run_gpu_tests_private.sh --exec`
from the CNA source cwd: **193 run, 191 passed, zero failed, two intentional platform skips**.
The Headless/Terminal contexts cannot host this selected renderer; the real SDL3 golden transcript
passes. Tests were built in SAMPLE-104's existing native tree via the sibling samples' off-by-default
`CNA_SAMPLES_BUILD_CNA_TESTS`, leaving the owner's HEADLESS tree untouched. A test-source glob used
the embedding project's root instead of its own directory and was corrected generally; no Metal
renderer was built or qualified. Initial full compilation recorded pre-existing nodiscard warnings
in unrelated runtime tests; final compilation reports three inherited GameTests nodiscard warnings;
none comes from the new code.

The new Dictionary entry-order repair belongs to Sharp Runtime (LP64 layout 64→176 bytes;
full consumer rebuild required). Native SAMPLE-104's seven static reference frames, including
help and bare-echo diagnostics, match XNA pixel for pixel. Dynamic timing values are not expected
to be equal. These runtime fixes do not implement browser account/discovery/relay transport or
qualify remote peers/gestures. The owner deferred SAMPLE-104 web completion with remote retained.

Evidence: `/rv/tmp/samples/SAMPLE-104-PerformanceUtility_4_0/evidence/implementation-20260928/`,
especially `cna-runtime-verified.log`, `cna-compat-tests-final.log`, numeric probes and accepted
original/native images. See sibling `samples/PerformanceUtility/{missing,diff}.md`.
