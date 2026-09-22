#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_gpu_test_isolation.md GTI-0007: name the tests that died on the graphics profile.

A test that throws a Reach-profile refusal before its first check -- the `SOFTWARE-213` class: a
GetBackBufferData, a second render target, a volume texture on a device that never asked for
HiDef -- fails exactly like a renderer defect. 103 Vulkan examples, then 23 more, then 32 WebGPU and
3 SDL_GPU ones sat in "known red" that way, filed as transfer or render-target bugs because of what
they were named (plans/plan_vulkan_parity.md VKPAR-0006/0018, plans/plan_gpu_test_isolation.md
GTI-0003). A test process launched is not a test that ran its assertions.

This reads ctest's own record of a run (Testing/Temporary/LastTest.log) and reports every test
that failed with one of CNA's profile-refusal messages in its output, or skipped with one in its
skip line (VMG-0004 found 17 CNAEXT examples skipping that way). It is a
classifier, not a verdict: such a test is a TEST defect (it must request the profile it uses)
until shown otherwise, and it must not be counted as a renderer result either way.

    profile_dead_tests.py <build-dir>|<LastTest.log>   exit 0 none found, 1 some found, 2 no log
    profile_dead_tests.py --self-test
"""

import re
import sys
from pathlib import Path

# The wording of every Reach-profile refusal CNA raises (modules/graphics/src/Xna: GraphicsDevice,
# Texture2D, TextureCube, Texture3D, RenderTargetCube, IndexBuffer). HiDef's own ceilings are not
# listed: exceeding them is a real limit, not a profile the test forgot to request.
REFUSAL = re.compile(
    r"by the Reach\s+graphics\s+profile"
    r"|GraphicsProfile\.Reach(?:'s own maximum| does not support| -- this is the profile)"
    r"|exceeds GraphicsProfile\.Reach"
    r"|not available (?:for a cube )?on GraphicsProfile\.Reach")

BLOCK_START = re.compile(r"^\d+/\d+ Testing: (.+)$")
PASSED = ("Test Passed.",)


def parse(text: str):
    """Yields (name, output, passed) for every test block in a LastTest.log."""
    name = None
    output = []
    in_output = False
    for line in text.splitlines():
        start = BLOCK_START.match(line)
        if start:
            name, output, in_output = start.group(1), [], False
            continue
        if name is None:
            continue
        if line == "Output:":
            in_output = True
            continue
        if line == "<end of output>":
            in_output = False
            continue
        if in_output:
            output.append(line)
            continue
        if line.startswith("Test Passed.") or line.startswith("Test Failed") or \
                line.startswith("Test Skipped") or "Test Not Run" in line or \
                line.startswith("Test Timeout") or line.startswith("Test Exception"):
            yield name, output, line.startswith(PASSED)
            name = None


SKIP_LINE = re.compile(r"\bSKIP\b")
# A leg a PASSING test skipped because an operation threw NotSupportedException -- the shape that
# hid GTI-0009's dead back-buffer legs, whose output names the exception type but not its message.
# Legitimate on a renderer that does not rasterize, so it is reported as a warning to look at.
UNAVAILABLE_LEG = re.compile(r"unavailable.*NotSupportedException")


def dead_tests(text: str):
    """Returns [(name, kind, first refusal line)] for tests that never reached their subject.

    A failed test whose output carries a profile refusal died on it. A test that SKIPPED (ctest
    records exit 77 as "Test Passed." in this log) is caught when its skip line itself names the
    refusal -- the CNAEXT examples turned any NotSupportedException into "no readable back buffer"
    and skipped, which read as a capability boundary for a renderer that has one (VMG-0004).
    A test that passes after deliberately provoking a refusal prints no SKIP and is not reported.
    """
    found = []
    for name, output, passed in parse(text):
        for line in output:
            if passed and UNAVAILABLE_LEG.search(line) and not REFUSAL.search(line):
                found.append((name, "WARN-LEG-UNAVAILABLE", line.strip()))
                break
            if not REFUSAL.search(line):
                continue
            if SKIP_LINE.search(line):
                found.append((name, "SKIPPED-ON-PROFILE", line.strip()))
                break
            if not passed:
                found.append((name, "DEAD-ON-PROFILE", line.strip()))
                break
    return found


SAMPLE = """Start testing: Sep 21 22:07 CEST
----------------------------------------------------------
1/7 Testing: WebGPU_ClearReadback
1/7 Test: WebGPU_ClearReadback
Output:
----------------------------------------------------------
terminate called after throwing an instance of 'System::NotSupportedException'
  what():  GetBackBufferData is not supported by the Reach graphics profile.
<end of output>
Test time =   0.40 sec
----------------------------------------------------------
Test Failed.
2/7 Testing: Vulkan_MrtMipFinalization
2/7 Test: Vulkan_MrtMipFinalization
Output:
----------------------------------------------------------
what(): SetRenderTargets: 2 render targets exceeds GraphicsProfile.Reach's own maximum of 1
<end of output>
Test time =   0.40 sec
----------------------------------------------------------
Test Failed.
3/7 Testing: Vulkan_RefusesUnderReach
3/7 Test: Vulkan_RefusesUnderReach
Output:
----------------------------------------------------------
[ OK ] caught: GetBackBufferData is not supported by the Reach graphics profile.
<end of output>
Test time =   0.10 sec
----------------------------------------------------------
Test Passed.
4/7 Testing: Vulkan_VolumeTooLarge
4/7 Test: Vulkan_VolumeTooLarge
Output:
----------------------------------------------------------
what(): Texture3D: 4096x4096x4096 exceeds GraphicsProfile.HiDef's own maximum volume extent of 2048
<end of output>
Test time =   0.10 sec
----------------------------------------------------------
Test Failed.
5/7 Testing: CNAEXT_Bloom
5/7 Test: CNAEXT_Bloom
Output:
----------------------------------------------------------
SKIP: this renderer has no readable back buffer (GetBackBufferData is not supported by the Reach graphics profile.)
<end of output>
Test time =   0.70 sec
----------------------------------------------------------
Test Passed.
6/7 Testing: Headless_Bloom
6/7 Test: Headless_Bloom
Output:
----------------------------------------------------------
SKIP: this renderer has no readable back buffer (this renderer does not rasterize)
<end of output>
Test time =   0.10 sec
----------------------------------------------------------
Test Passed.
7/7 Testing: Vulkan_BoundTargetLifetime
7/7 Test: Vulkan_BoundTargetLifetime
Output:
----------------------------------------------------------
[INFO] A1: oracle unavailable on VULKAN (NotSupportedException) -- boundary recorded
<end of output>
Test time =   0.30 sec
----------------------------------------------------------
Test Passed.
"""


def self_test() -> int:
    found = dead_tests(SAMPLE)
    names = [f"{name}:{kind}" for name, kind, _ in found]
    expected = ["WebGPU_ClearReadback:DEAD-ON-PROFILE", "Vulkan_MrtMipFinalization:DEAD-ON-PROFILE",
                "CNAEXT_Bloom:SKIPPED-ON-PROFILE", "Vulkan_BoundTargetLifetime:WARN-LEG-UNAVAILABLE"]
    if names != expected:
        print(f"profile_dead_tests self-test FAILED: found {names}, expected {expected}")
        return 1
    print("profile_dead_tests self-test: a failed and a skipped Reach refusal and a passing test's "
          "NotSupported leg are found; a passing refusal test, a HiDef limit and a genuine capability "
          "skip are not")
    return 0


def main(argv) -> int:
    if len(argv) == 2 and argv[1] == "--self-test":
        return self_test()
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path = Path(argv[1])
    if path.is_dir():
        path = path / "Testing" / "Temporary" / "LastTest.log"
    if not path.is_file():
        print(f"profile_dead_tests: no ctest log at {path}", file=sys.stderr)
        return 2
    found = dead_tests(path.read_text(encoding="utf-8", errors="replace"))
    warnings = [entry for entry in found if entry[1].startswith("WARN")]
    found = [entry for entry in found if not entry[1].startswith("WARN")]
    for name, kind, line in warnings:
        print(f"  {kind}  {name}: {line}  (a leg skipped on NotSupportedException -- check its profile)")
    if not found:
        print("profile_dead_tests: no test died on, or skipped because of, a graphics-profile refusal")
        return 0
    print(f"profile_dead_tests: {len(found)} test(s) never reached their assertions because of a "
          "graphics-profile refusal -- a TEST defect (request the profile the test uses), not a renderer result:")
    for name, kind, line in found:
        print(f"  {kind}  {name}: {line}")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
