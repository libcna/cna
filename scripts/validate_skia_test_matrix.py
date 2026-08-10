#!/usr/bin/env python3
"""Validate the SKIA-2 EasyGL test/golden/oracle classification matrix."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections import Counter


ALLOWED_KINDS = {"ctest", "tool", "golden", "oracle"}
ALLOWED_CATEGORIES = {"2d-direct", "2d-emulation", "3d", "device-dependent"}

# These tests are classified by the older SKIA-2 matrix according to their primary dependency,
# but their capability is also an input to the SKIA-95 3D decision.  Keep this closed and exact:
# adding every device-dependent test would incorrectly pull window/presenter behavior into 3D.
THREED_DEVICE_CROSSCUTS = {
    "ctest:EasyGL_RenderTarget2D_MsaaResolve",
    "ctest:EasyGL_OcclusionQuery_Cycle",
    "ctest:EasyGL_OcclusionQuery_VisibleQuad",
    "ctest:EasyGL_OcclusionQuery_OccludedQuad",
    "ctest:EasyGL_RenderTargetCube_DepthFormat",
    "ctest:EasyGL_MSAA_4x_Readback",
    "ctest:EasyGL_DepthFormat",
    "ctest:EasyGL_MsaaChange",
    "ctest:EasyGL_TextureAnisotropic_DualTextureEffect",
    "ctest:EasyGL_Texture2D_AnisotropicSingleLevel",
    "ctest:EasyGL_Anisotropic_GlState",
    "ctest:EasyGL_GraphicsDevice_DefaultStateOcclusion",
    "ctest:EasyGL_MsaaDepthContract",
    "ctest:EasyGL_MsaaFirstReadback",
    "ctest:EasyGL_MsaaMipReadback",
    "ctest:EasyGL_RenderTargetCube_MsaaFace",
}


# Rules are intentionally additive: an effect fixture normally needs geometry, transforms,
# textures, states and shading.  They inspect the stable entry plus its human-written evidence.
# There is no catch-all rule, so an unfamiliar 3D registration fails the audit.
THREED_FEATURE_RULES = (
    ("3D-VERTEX-LAYOUT", r"vertex|geometry|mesh|quad|triangle|normal|bone|skin|model|primitive|winding|cull|golden|3d (?:stream|sample|draw|first|pass|viewport|scissor)|draw legs"),
    ("3D-VERTEX-BUFFER", r"vertex.?buffer|vertex/index|dynamic .*buffer|buffer (?:usage|lifetime)|cpu transfer|missing-binding|3d draw legs|3d .* legs|mesh"),
    ("3D-INDEX-16", r"vertex/index textured|indexed normal|model mesh|index.?buffer|getdata|house3d"),
    ("3D-INDEX-32", r"32.?bit|primitives_32|index path.*32"),
    ("3D-DRAW-USER", r"drawuser|user vertex|user indexed|custom user"),
    ("3D-PRIMITIVE-TRIANGLE", r"triangle|quad|geometry|mesh|golden|3d (?:sample|draw|first|pass|viewport|scissor)|draw legs|producer/consumer|consumer|primitive"),
    ("3D-PRIMITIVE-STRIP", r"triangle.?strip|trianglestrip"),
    ("3D-PRIMITIVE-LINE", r"line.?list|line.?strip|linelist|linestrip|house3d"),
    ("3D-INSTANCING", r"instanc"),
    ("3D-TRANSFORM", r"transform|world|view|camera|eye|bone|geometry|model|winding|normal|golden|3d (?:stream|sample|draw|first|pass|viewport|scissor)|draw legs"),
    ("3D-CLIP-INTERPOLATE", r"geometry|quad|triangle|mesh|golden|3d stream|3d .* legs|vertex pipeline|winding|culling|primitive scene"),
    ("3D-TEXTURE-2D", r"textured|texture(?!cube|3d)|sampling|sampler|pbr|material|dual.?texture|alpha.?test"),
    ("3D-TEXTURE-CUBE", r"cube|environment|envmap"),
    ("3D-TEXTURE-VOLUME", r"volume|texture3d"),
    ("3D-SAMPLER-MIP", r"sampler|sampling|filter|address|mip|texture slot"),
    ("3D-SAMPLER-ANISOTROPY", r"anisotrop"),
    ("3D-STATE-DEPTH", r"depth|shadow"),
    ("3D-STATE-STENCIL", r"stencilenable|stencilmask|stencilops|stenciltwosided|referencestencil|clearstencil|depth/stencil|stencil storage|stencil operation|stencil reference|stencil attachment"),
    ("3D-STATE-CULL-FILL", r"cull|winding|front.?face|fill mode|wireframe|rasterization bias|depthbias|depth bias"),
    ("3D-STATE-BLEND-COLOR", r"alpha|vertex.?colou?r|tint|blend|emissive|colou?r space|colou?r write"),
    ("3D-STATE-MRT", r"\bmrt\b|multiple simultaneous|two attachments|2.?4 target"),
    ("3D-STATE-MSAA", r"msaa|sample.?count|resolve"),
    ("3D-STATE-ORDER", r"order|deferred|lifecycle|bound target|first.?use|producer/consumer|backbuffer consumer|pass legs"),
    ("3D-VIEWPORT-SCISSOR", r"viewport|scissor"),
    ("3D-FX-BASIC", r"basiceffect|basic effect|stock effect|stock-effect|lit textured|default lighting|movingquad3d|keyboardcube3d|colou?red3d"),
    ("3D-FX-ALPHATEST", r"alpha.?test"),
    ("3D-FX-DUAL", r"dual.?texture"),
    ("3D-FX-ENVMAP", r"environment|envmap"),
    ("3D-FX-SKINNED", r"skinned.?effect|skinned geometry|skinned vertex|skinned shader|skinned world|skinned pbr"),
    ("3D-FX-PBR", r"\bpbr\b|pbreffect"),
    ("3D-FX-CUSTOM", r"shadereffect|_shader\b|arbitrary shader|arbitrary .*program|custom effect"),
    ("3D-SHADE-LIGHT", r"light|emissive|specular|phong|lambert|normal|fresnel|reflection"),
    ("3D-SHADE-FOG", r"\bfog\b|fog geometry|geometry/fog"),
    ("3D-MODEL-SKIN", r"model|avatar|skin|bone|skeleton|hierarchy|mesh"),
    ("3D-QUERY-OCCLUSION", r"occlusion|pixelcount"),
    ("3D-RESOURCE-CONTRACT", r"propert|validation|null|disposed|lifetime|capacity|contract|getdata|setdata|first.?use|missing-binding|buffer usage|stress|readback"),
    ("3D-TARGET-PASS", r"render.?target|target |backbuffer|shadow.?map|attachment|pass |clear|producer/consumer|cube-face"),
)

# The demo name carries less detail than its source: it uses VPC, vertex/index buffers,
# TriangleList, LineList and BasicEffect.  The two tools are intentionally mixed entry points.
THREED_EXACT_FEATURES = {
    "ctest:EasyGL_House3D_SmokeTest": {
        "3D-VERTEX-LAYOUT", "3D-VERTEX-BUFFER", "3D-INDEX-16",
        "3D-PRIMITIVE-TRIANGLE", "3D-PRIMITIVE-LINE", "3D-TRANSFORM",
        "3D-CLIP-INTERPOLATE", "3D-FX-BASIC",
    },
    "tool:cna_diag_easygl": {
        "3D-VERTEX-LAYOUT", "3D-VERTEX-BUFFER", "3D-INDEX-16",
        "3D-PRIMITIVE-TRIANGLE", "3D-TRANSFORM", "3D-CLIP-INTERPOLATE",
        "3D-TEXTURE-2D", "3D-FX-BASIC", "3D-STATE-DEPTH",
    },
    "tool:cna_oracle_render_easygl": {
        "3D-VERTEX-LAYOUT", "3D-VERTEX-BUFFER", "3D-INDEX-16",
        "3D-PRIMITIVE-TRIANGLE", "3D-PRIMITIVE-STRIP", "3D-PRIMITIVE-LINE",
        "3D-TRANSFORM", "3D-CLIP-INTERPOLATE", "3D-TEXTURE-2D",
        "3D-TEXTURE-CUBE", "3D-STATE-CULL-FILL", "3D-FX-BASIC",
        "3D-FX-ALPHATEST", "3D-FX-DUAL", "3D-FX-ENVMAP",
        "3D-FX-SKINNED", "3D-SHADE-LIGHT", "3D-SHADE-FOG",
    },
    "ctest:EasyGL_ColorSpace_MidTone": {"3D-FX-BASIC"},
}


def expected_entries(root: pathlib.Path) -> set[str]:
    cmake = (root / "cmake/Tests/EasyGLTests.cmake").read_text()
    cmake = re.sub(r"#[^\n]*", "", cmake)
    tests = re.findall(
        r"\bcna_register_renderer_test\s*\(\s*NAME\s+([A-Za-z0-9_]+)", cmake
    )
    tools = re.findall(
        r"\bcna_easygl_test\s*\(\s*(cna_diag_easygl|cna_oracle_render_easygl)\b", cmake
    )
    expected = {f"ctest:{name}" for name in tests}
    expected.update(f"tool:{name}" for name in tools)
    golden_root = root / "examples/golden"
    expected.update(f"golden:{path.name}" for path in golden_root.glob("*.png"))
    oracle_root = root / "tools/xna-oracle/scenes"
    expected.update(f"oracle:{path.name}" for path in oracle_root.glob("*.scene"))
    if len(tests) != len(set(tests)):
        duplicates = sorted(name for name, count in Counter(tests).items() if count > 1)
        raise ValueError("duplicate EasyGL CTest registrations: " + ", ".join(duplicates))
    return expected


def matrix_entries(matrix: pathlib.Path) -> tuple[list[str], list[str]]:
    entries: list[str] = []
    errors: list[str] = []
    for line_number, line in enumerate(matrix.read_text().splitlines(), start=1):
        if not line.startswith("| `"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 4:
            errors.append(f"{matrix}:{line_number}: expected four table cells")
            continue
        entry_match = re.fullmatch(r"`([^`]+)`", cells[0])
        kind_match = re.fullmatch(r"`([^`]+)`", cells[1])
        category_match = re.fullmatch(r"`([^`]+)`", cells[2])
        if not entry_match or not kind_match or not category_match:
            errors.append(f"{matrix}:{line_number}: first three cells must use code spans")
            continue
        entry = entry_match.group(1)
        kind = kind_match.group(1)
        category = category_match.group(1)
        if kind not in ALLOWED_KINDS:
            errors.append(f"{matrix}:{line_number}: invalid kind {kind!r}")
        if category not in ALLOWED_CATEGORIES:
            errors.append(f"{matrix}:{line_number}: invalid category {category!r}")
        if not entry.startswith(kind + ":"):
            errors.append(f"{matrix}:{line_number}: entry prefix does not match kind {kind!r}")
        if not cells[3]:
            errors.append(f"{matrix}:{line_number}: Skia route/evidence is required")
        entries.append(entry)
    return entries, errors


def matrix_rows(matrix: pathlib.Path) -> list[tuple[str, str, str, str]]:
    rows: list[tuple[str, str, str, str]] = []
    for line in matrix.read_text().splitlines():
        if not line.startswith("| `"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 4:
            continue
        matches = [re.fullmatch(r"`([^`]+)`", cell) for cell in cells[:3]]
        if all(matches):
            rows.append((matches[0].group(1), matches[1].group(1),
                         matches[2].group(1), cells[3]))
    return rows


def documented_3d_features(document: pathlib.Path) -> set[str]:
    features: set[str] = set()
    for line in document.read_text().splitlines():
        match = re.match(r"\| `(3D-[A-Z0-9-]+)` \|", line)
        if match:
            features.add(match.group(1))
    return features


def required_3d_features(entry: str, evidence: str) -> set[str]:
    haystack = f"{entry} {evidence}".lower()
    features = {
        feature for feature, pattern in THREED_FEATURE_RULES
        if re.search(pattern, haystack, re.IGNORECASE)
    }
    features.update(THREED_EXACT_FEATURES.get(entry, set()))
    # These oracle names contain "quad" as the four-endpoint/four-vertex shape, but their
    # declared topology is exclusively a line or strip; do not infer TriangleList from "quad".
    if entry.startswith("oracle:colored_line") or entry == "oracle:colored_trianglestrip_quad.scene":
        features.discard("3D-PRIMITIVE-TRIANGLE")
    return features


def validate_3d_requirements(
    root: pathlib.Path, rows: list[tuple[str, str, str, str]]
) -> tuple[list[tuple[str, str, tuple[str, ...]]], list[str]]:
    errors: list[str] = []
    by_entry = {entry: (kind, category, evidence) for entry, kind, category, evidence in rows}
    for entry in sorted(THREED_DEVICE_CROSSCUTS):
        if entry not in by_entry:
            errors.append(f"missing SKIA-95 device cross-cut entry: {entry}")
        elif by_entry[entry][1] != "device-dependent":
            errors.append(f"SKIA-95 cross-cut must remain device-dependent: {entry}")

    feature_document = root / "docs/skia-3d-call-effect-matrix.md"
    try:
        documented = documented_3d_features(feature_document)
    except OSError as error:
        return [], [f"Skia 3D requirement-matrix read failed: {error}"]
    rule_features = {feature for feature, _ in THREED_FEATURE_RULES}
    exact_features = set().union(*THREED_EXACT_FEATURES.values())
    known = rule_features | exact_features
    for feature in sorted(known - documented):
        errors.append(f"undocumented SKIA-95 feature ID: {feature}")
    for feature in sorted(documented - known):
        errors.append(f"documented SKIA-95 feature has no audit rule: {feature}")

    mapped: list[tuple[str, str, tuple[str, ...]]] = []
    used: set[str] = set()
    for entry, _kind, category, evidence in rows:
        if category != "3d" and entry not in THREED_DEVICE_CROSSCUTS:
            continue
        features = required_3d_features(entry, evidence)
        if not features:
            errors.append(f"unmapped SKIA-95 3D requirement: {entry}")
            continue
        unknown = features - documented
        if unknown:
            errors.append(f"{entry}: unknown SKIA-95 features: {', '.join(sorted(unknown))}")
        used.update(features)
        mapped.append((entry, category, tuple(sorted(features))))
    for feature in sorted(documented - used):
        errors.append(f"SKIA-95 feature has no live EasyGL evidence: {feature}")
    return mapped, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--dump", action="store_true")
    parser.add_argument("--dump-3d", action="store_true",
                        help="print the exact SKIA-95 entry-to-feature expansion")
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        expected = expected_entries(root)
    except (OSError, ValueError) as error:
        print(f"Skia test-matrix extraction failed: {error}", file=sys.stderr)
        return 2
    if args.dump:
        print("\n".join(sorted(expected)))
        return 0
    matrix = root / "docs/skia-easygl-test-matrix.md"
    try:
        actual, errors = matrix_entries(matrix)
    except OSError as error:
        print(f"Skia test-matrix read failed: {error}", file=sys.stderr)
        return 2
    duplicates = sorted(entry for entry, count in Counter(actual).items() if count > 1)
    missing = sorted(expected - set(actual))
    extra = sorted(set(actual) - expected)
    try:
        rows = matrix_rows(matrix)
        mapped_3d, requirement_errors = validate_3d_requirements(root, rows)
    except OSError as error:
        print(f"Skia 3D requirement-matrix extraction failed: {error}", file=sys.stderr)
        return 2
    errors.extend(requirement_errors)
    for error in errors:
        print(error, file=sys.stderr)
    if duplicates:
        print("duplicate matrix entries:\n  " + "\n  ".join(duplicates), file=sys.stderr)
    if missing:
        print("missing matrix entries:\n  " + "\n  ".join(missing), file=sys.stderr)
    if extra:
        print("stale/unknown matrix entries:\n  " + "\n  ".join(extra), file=sys.stderr)
    if errors or duplicates or missing or extra:
        return 1
    if args.dump_3d:
        print("| Entry | Matrix category | Required SKIA-95 features |")
        print("|---|---|---|")
        for entry, category, features in mapped_3d:
            feature_text = ", ".join(f"`{feature}`" for feature in features)
            print(f"| `{entry}` | `{category}` | {feature_text} |")
        return 0
    category_counts: Counter[str] = Counter()
    for line in matrix.read_text().splitlines():
        if line.startswith("| `"):
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            if len(cells) == 4:
                match = re.fullmatch(r"`([^`]+)`", cells[2])
                if match:
                    category_counts[match.group(1)] += 1
    summary = ", ".join(f"{name}={category_counts[name]}" for name in sorted(ALLOWED_CATEGORIES))
    print(f"Skia test matrix covers {len(expected)} entries ({summary})")
    primary_3d = sum(1 for _entry, category, _features in mapped_3d if category == "3d")
    crosscuts = len(mapped_3d) - primary_3d
    feature_count = len({feature for _entry, _category, features in mapped_3d for feature in features})
    print(f"SKIA-95 maps {primary_3d}/{category_counts['3d']} 3D entries plus "
          f"{crosscuts} device cross-cuts across {feature_count} required features")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
