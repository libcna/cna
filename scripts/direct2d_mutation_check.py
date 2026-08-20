#!/usr/bin/env python3
"""plans/plan_direct2d.md D2D-127: prove the Direct2D tests actually detect the mutations they claim to.

A green suite says nothing until you know it can go red. This harness introduces one deliberate
defect at a time into the renderer, rebuilds, runs the CTest that is supposed to catch it, and
requires that test to FAIL. A mutation that survives is a coverage hole, not a success.

Usage:
  direct2d_mutation_check.py --list
  direct2d_mutation_check.py --dry-run [<repository-root>]
  direct2d_mutation_check.py --run <build-dir> [<repository-root>]

`--dry-run` only applies and reverts each mutation, verifying that its anchor text still exists
exactly once. That is the check that goes stale: a refactor can silently make a mutation a no-op,
after which it would "survive" for a reason that has nothing to do with test coverage. It needs no
Windows, no Wine, and no build, so it can run anywhere.

`--run` additionally rebuilds and runs the named test for each mutation, and is the full gate. It
needs a configured Direct2D build directory and a runtime able to execute the tests.
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import subprocess
import sys


RENDERER = "modules/renderers/direct2d/src/Direct2DRenderer.cpp"


@dataclasses.dataclass(frozen=True)
class Mutation:
    """One deliberate defect and the test that must notice it."""

    name: str
    path: str
    original: str
    mutated: str
    expected_failing_test: str
    rationale: str


MUTATIONS: tuple[Mutation, ...] = (
    Mutation(
        name="blend-factor-mapping",
        path=RENDERER,
        original="if (colorSrcBlend == 0 && colorDstBlend == 1) return Direct2DBlendMode::Copy;       // Opaque",
        mutated="if (colorSrcBlend == 0 && colorDstBlend == 1) return Direct2DBlendMode::SourceOver; // Opaque",
        expected_failing_test="Direct2D_Unit",
        rationale="BlendState::Opaque silently composited as source-over instead of copy.",
    ),
    Mutation(
        name="porter-duff-source-atop",
        path=RENDERER,
        original="""            case Direct2DBlendMode::SourceAtop:
                sourceFactor = destinationAlpha;
                destinationFactor = 1.0 - sourceAlpha;
                break;""",
        mutated="""            case Direct2DBlendMode::SourceAtop:
                sourceFactor = 1.0 - destinationAlpha;
                destinationFactor = sourceAlpha;
                break;""",
        expected_failing_test="Direct2D_Unit",
        rationale="Source-atop given destination-atop's coefficients in the algebraic oracle.",
    ),
    Mutation(
        name="presentation-letterbox-scale",
        path=RENDERER,
        original="                result.scaleX = result.scaleY = std::min(sx, sy);",
        mutated="                result.scaleX = result.scaleY = std::max(sx, sy);",
        expected_failing_test="Direct2D_2DParity",
        rationale="Letterbox computed with overscan's scale, so content overflows instead of fitting.",
    ),
    Mutation(
        name="mip-level-rounding",
        path=RENDERER,
        original="        return std::max(0, static_cast<int>(std::floor(std::log2(1.0 / sigma.sigmaMin))));",
        mutated="        return std::max(0, static_cast<int>(std::ceil(std::log2(1.0 / sigma.sigmaMin))));",
        expected_failing_test="Direct2D_Unit",
        rationale="Mip selection rounds to the next smaller level, sampling a blurrier one.",
    ),
    Mutation(
        name="colour-write-mask-validation",
        path=RENDERER,
        original="            if (writeState.colorWriteChannels[target] != 15)",
        mutated="            if (writeState.colorWriteChannels[target] != -1)",
        expected_failing_test="Direct2D_2DParity",
        rationale="An unsupported ColorWriteChannels mask is accepted instead of rejected.",
    ),
    Mutation(
        name="readback-channel-order",
        path=RENDERER,
        original="""                destinationRow[x * 4 + 0] = sourceRow[x * 4 + 2];
                destinationRow[x * 4 + 1] = sourceRow[x * 4 + 1];
                destinationRow[x * 4 + 2] = sourceRow[x * 4 + 0];
                destinationRow[x * 4 + 3] = sourceRow[x * 4 + 3];
            }
        }
        return rgba;""",
        mutated="""                destinationRow[x * 4 + 0] = sourceRow[x * 4 + 0];
                destinationRow[x * 4 + 1] = sourceRow[x * 4 + 1];
                destinationRow[x * 4 + 2] = sourceRow[x * 4 + 2];
                destinationRow[x * 4 + 3] = sourceRow[x * 4 + 3];
            }
        }
        return rgba;""",
        expected_failing_test="Direct2D_Unit",
        rationale="Readback returns BGRA bytes as if they were RGBA, swapping red and blue.",
    ),
    Mutation(
        name="blend-factor-validation",
        path=RENDERER,
        original="""        if (std::abs(r - 1.0f) > epsilon || std::abs(g - 1.0f) > epsilon ||
            std::abs(b - 1.0f) > epsilon || std::abs(a - 1.0f) > epsilon)""",
        mutated="""        if (false && (std::abs(r - 1.0f) > epsilon || std::abs(g - 1.0f) > epsilon ||
            std::abs(b - 1.0f) > epsilon || std::abs(a - 1.0f) > epsilon))""",
        expected_failing_test="Direct2D_2DParity",
        rationale="An unsupported non-white GraphicsDevice.BlendFactor is accepted instead of rejected.",
    ),
)


def apply_mutation(root: pathlib.Path, mutation: Mutation) -> str:
    """Applies one mutation and returns the original file contents."""
    path = root / mutation.path
    text = path.read_text(encoding="utf-8")
    occurrences = text.count(mutation.original)
    if occurrences != 1:
        raise RuntimeError(
            f"mutation '{mutation.name}' anchors on text occurring {occurrences} times in "
            f"{mutation.path}; it must occur exactly once or the mutation is stale"
        )
    path.write_text(text.replace(mutation.original, mutation.mutated, 1), encoding="utf-8")
    return text


def revert(root: pathlib.Path, mutation: Mutation, original_text: str) -> None:
    (root / mutation.path).write_text(original_text, encoding="utf-8")


def run_expected_failure(build_dir: str, mutation: Mutation) -> tuple[bool, str]:
    """Builds and runs the mutation's test; returns (test_failed_as_required, detail)."""
    build = subprocess.run(
        ["cmake", "--build", build_dir, "--parallel", "2"],
        capture_output=True, text=True, check=False,
    )
    if build.returncode != 0:
        return False, "the mutated tree did not build, so the test could not judge it"
    test = subprocess.run(
        ["ctest", "--test-dir", build_dir, "-R", f"^{mutation.expected_failing_test}$",
         "--output-on-failure"],
        capture_output=True, text=True, check=False,
    )
    if test.returncode == 0:
        return False, f"{mutation.expected_failing_test} still passed -- the mutation survived"
    return True, f"{mutation.expected_failing_test} failed as required"


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="print the mutation catalogue")
    parser.add_argument("--dry-run", action="store_true", help="only verify each anchor still applies")
    parser.add_argument("--run", metavar="BUILD_DIR", help="build and run each mutation's test")
    parser.add_argument("root", nargs="?", default=None)
    arguments = parser.parse_args(argv[1:])

    root = pathlib.Path(arguments.root or pathlib.Path(__file__).resolve().parents[1]).resolve()

    if arguments.list:
        for mutation in MUTATIONS:
            print(f"{mutation.name}: {mutation.rationale} -> {mutation.expected_failing_test}")
        return 0

    if not arguments.dry_run and not arguments.run:
        parser.print_help()
        return 2

    failures: list[str] = []
    for mutation in MUTATIONS:
        try:
            original_text = apply_mutation(root, mutation)
        except (OSError, RuntimeError) as error:
            failures.append(str(error))
            continue
        try:
            if arguments.run:
                detected, detail = run_expected_failure(arguments.run, mutation)
                print(f"[{'PASS' if detected else 'SURVIVED'}] {mutation.name}: {detail}")
                if not detected:
                    failures.append(f"mutation '{mutation.name}' survived: {detail}")
            else:
                print(f"[APPLIES] {mutation.name} -> {mutation.expected_failing_test}")
        finally:
            revert(root, mutation, original_text)

    for failure in failures:
        print(f"Direct2D mutation check: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"Direct2D mutation check: {len(MUTATIONS)} mutation(s) accounted for.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
