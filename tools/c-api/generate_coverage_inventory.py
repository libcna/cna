#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL

"""Generate the complete public C++ to C API coverage inventory.

The scanner deliberately uses Doxygen's C++ parser instead of a collection of regular
expressions.  Doxygen expands CNA's SharpRuntime property macros, preserves overload signatures,
reports access levels and emits stable source locations.  The generated Markdown remains a
reviewable snapshot; ``--check`` is a mandatory gate (CBIND-043): the CTest test
``CApiCoverageMatrix`` and the ``c-api-coverage-gate`` workflow both run it.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path
from typing import Iterable


SCHEMA_VERSION = 1
PUBLIC_ROOTS = ("Microsoft", "CNA")
EXCLUDED_PATH_SEGMENTS = ("Internal", "Detail")

# CBIND-047, decided by the project owner on 2026-08-16: the platform-abstraction module is a
# substrate the C ABI is built ON, not a surface it exposes. A C caller reaches platform behaviour
# through the routes that use it -- a window through the game, a camera through the devices
# surface, an input snapshot through cna_keyboard_get_state -- and never through IPlatform, which
# deals in C++ interfaces, unique_ptr ownership and virtual dispatch that have no C form at all.
# That is the same argument the Internal/Detail segments already encode, and it is written here as
# a whole-module rule because the platform module's public headers are its internal contract: the
# renderers and the runtime are its consumers, not applications.
#
# This is deliberately narrow and deliberately visible. It removes 1,196 rows from the inventory,
# and a scope rule that large has to be a recorded decision rather than a quiet widening of an
# exclusion list -- which is why it names its owner, its date and its reason here and is reported
# by name in COVERAGE.md.
EXCLUDED_MODULES = ("platform",)
STABLE_ID_PATTERN = re.compile(r"CPP-[0-9A-F]{12}")
COMPOUND_KINDS = {"class", "struct", "union"}
# Doxygen moves namespace-level declarations into a group compound when a public header wraps them
# in @addtogroup. Keep those declarations in the inventory; the source location and qualified name
# still identify the original public API, and the identity de-duplication below collapses any copy
# that Doxygen also leaves in the namespace/file compound.
MEMBER_COMPOUND_KINDS = COMPOUND_KINDS | {"namespace", "file", "group"}
PUBLIC_ACCESS = {"public", "protected", None}
STATUS_ORDER = ("implemented", "partial", "planned", "not-applicable")


@dataclasses.dataclass(frozen=True)
class Symbol:
    header: str
    line: int
    kind: str
    qualified_name: str
    signature: str
    display: str
    access: str
    type_text: str

    @property
    def identity(self) -> str:
        return "|".join((self.header, self.kind, self.qualified_name, self.signature))

    @property
    def stable_id(self) -> str:
        digest = hashlib.sha256(self.identity.encode("utf-8")).hexdigest()[:12].upper()
        return f"CPP-{digest}"


@dataclasses.dataclass(frozen=True)
class Mapping:
    mapping: str
    tests: str
    status: str
    task: str
    rule_id: str | None


@dataclasses.dataclass(frozen=True)
class Rule:
    rule_id: str
    qualified_name: re.Pattern[str]
    signature: re.Pattern[str] | None
    header: re.Pattern[str] | None
    kinds: frozenset[str]
    mapping: str
    tests: str
    status: str
    task: str
    approved_symbols: frozenset[str]

    def matches_pattern(self, symbol: Symbol) -> bool:
        return (
            (not self.kinds or symbol.kind in self.kinds)
            and self.qualified_name.fullmatch(symbol.qualified_name) is not None
            and (self.signature is None or self.signature.fullmatch(symbol.signature) is not None)
            and (self.header is None or self.header.fullmatch(symbol.header) is not None)
        )

    def matches(self, symbol: Symbol, *, ignore_approval: bool = False) -> bool:
        """Whether this rule speaks for the symbol.

        A rule's patterns say which symbols it *may* cover; ``approved_symbols`` says which ones
        an owner actually reviewed.  Both are required, because many rules are deliberately broad
        -- ``.*`` over one header, asserting that the header's whole contract is bound -- and a
        symbol added to that header afterwards was reviewed by nobody.  Without the second half a
        merge can silently inherit "implemented and tested" for routes that do not exist; that is
        not hypothetical, it is what merging `next` did to 121 symbols (CBIND-050).
        """
        if not self.matches_pattern(symbol):
            return False
        return ignore_approval or symbol.stable_id in self.approved_symbols


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def normalize_space(value: str) -> str:
    return re.sub(r"\s+", " ", value).strip()


def xml_text(element: ET.Element | None) -> str:
    if element is None:
        return ""
    return normalize_space("".join(element.itertext()))


def path_is_explicitly_internal(relative_to_include: Path) -> bool:
    return any(segment in EXCLUDED_PATH_SEGMENTS for segment in relative_to_include.parts)


def discover_headers(root: Path) -> tuple[list[Path], list[Path]]:
    included: list[Path] = []
    excluded: list[Path] = []
    modules_root = root / "modules"
    for module in sorted(path for path in modules_root.iterdir() if path.is_dir()):
        include_root = module / "include"
        if not include_root.is_dir():
            continue
        if module.name in EXCLUDED_MODULES:
            for header in sorted((include_root).rglob("*.hpp")):
                excluded.append(header.relative_to(root))
            continue
        for public_root in PUBLIC_ROOTS:
            candidate_root = include_root / public_root
            if not candidate_root.is_dir():
                continue
            for header in sorted(candidate_root.rglob("*.hpp")):
                relative_to_include = header.relative_to(include_root)
                if path_is_explicitly_internal(relative_to_include):
                    excluded.append(header.relative_to(root))
                else:
                    included.append(header.relative_to(root))

    duplicates = [path for path, count in Counter(included).items() if count != 1]
    if duplicates:
        raise RuntimeError(f"Public header discovery produced duplicates: {duplicates}")
    if not included:
        raise RuntimeError("No public CNA C++ headers were discovered.")
    return sorted(included), sorted(excluded)


def discover_include_paths(root: Path) -> list[Path]:
    candidates: set[Path] = set()
    for base in (root / "modules", root.parent / "sharp-runtime" / "modules"):
        if not base.is_dir():
            continue
        for candidate in base.glob("*/include"):
            if candidate.is_dir():
                candidates.add(candidate.resolve())

    extra_candidates = (
        root.parent / "sharp-runtime" / "vendor",
        root / ".sdl-prebuilt-Linux-x86_64" / "install" / "include",
        root / "third_party" / "enet" / "include",
    )
    candidates.update(path.resolve() for path in extra_candidates if path.is_dir())
    return sorted(candidates)


def run_doxygen(root: Path, headers: list[Path], output_directory: Path) -> Path:
    doxygen = shutil.which("doxygen")
    if doxygen is None:
        raise RuntimeError(
            "Doxygen is required to regenerate the C API coverage inventory (tested with 1.9.8+)."
        )

    include_paths = discover_include_paths(root)
    config_lines = (
        "PROJECT_NAME = CNA_C_API_Coverage",
        f"OUTPUT_DIRECTORY = {output_directory}",
        "INPUT = " + " ".join(path.as_posix() for path in headers),
        "FILE_PATTERNS = *.hpp",
        "RECURSIVE = NO",
        "GENERATE_XML = YES",
        "GENERATE_HTML = NO",
        "GENERATE_LATEX = NO",
        "XML_PROGRAMLISTING = NO",
        "EXTRACT_ALL = YES",
        "EXTRACT_PRIVATE = NO",
        "EXTRACT_PACKAGE = YES",
        "EXTRACT_STATIC = YES",
        "EXTRACT_LOCAL_CLASSES = NO",
        "HIDE_UNDOC_MEMBERS = NO",
        "ENABLE_PREPROCESSING = YES",
        "MACRO_EXPANSION = YES",
        "EXPAND_ONLY_PREDEF = NO",
        "SEARCH_INCLUDES = YES",
        "INCLUDE_PATH = " + " ".join(path.as_posix() for path in include_paths),
        "INCLUDE_FILE_PATTERNS = *.h *.hpp",
        "PREDEFINED = CNAEXT= NOXNA= SOUND_ENABLED XNA5 CNA_CNAEXT CNA_DEVICES CNA_RENDERER_HEADLESS",
        "SKIP_FUNCTION_MACROS = NO",
        "HAVE_DOT = NO",
        "SHOW_FILES = NO",
        "SHOW_NAMESPACES = NO",
        "QUIET = YES",
        "WARNINGS = NO",
    )
    completed = subprocess.run(
        [doxygen, "-"],
        cwd=root,
        input="\n".join(config_lines) + "\n",
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    xml_directory = output_directory / "xml"
    if completed.returncode != 0 or not (xml_directory / "index.xml").is_file():
        diagnostics = (completed.stdout + completed.stderr).strip()
        raise RuntimeError(
            f"Doxygen coverage parse failed with exit code {completed.returncode}:\n{diagnostics}"
        )
    return xml_directory


def normalize_location(root: Path, location: ET.Element | None) -> tuple[str, int] | None:
    if location is None:
        return None
    raw_file = location.get("file", "")
    if not raw_file:
        return None
    path = Path(raw_file)
    if path.is_absolute():
        try:
            path = path.relative_to(root)
        except ValueError:
            return None
    try:
        line = int(location.get("line", "0"))
    except ValueError:
        line = 0
    return path.as_posix(), line


def template_signature(element: ET.Element) -> str:
    parameters = []
    for parameter in element.findall("./templateparamlist/param"):
        parameter_text = xml_text(parameter.find("type"))
        declaration_name = xml_text(parameter.find("declname"))
        if declaration_name and declaration_name not in parameter_text:
            parameter_text = normalize_space(f"{parameter_text} {declaration_name}")
        parameters.append(parameter_text)
    return f"template<{', '.join(parameters)}> " if parameters else ""


def classify_function(compound_kind: str, compound_name: str, name: str) -> str:
    unqualified_compound = compound_name.rsplit("::", 1)[-1].split("<", 1)[0]
    if name == unqualified_compound:
        return "constructor"
    if name == f"~{unqualified_compound}":
        return "destructor"
    if name.startswith("operator"):
        return "operator"
    return "function" if compound_kind == "namespace" else "method"


def symbol_from_member(
    root: Path,
    compound_kind: str,
    compound_name: str,
    member: ET.Element,
) -> Symbol | None:
    if member.get("prot") not in PUBLIC_ACCESS:
        return None
    location = normalize_location(root, member.find("location"))
    if location is None:
        return None
    header, line = location
    member_kind = member.get("kind", "unknown")
    name = xml_text(member.find("name"))
    qualified_name = xml_text(member.find("qualifiedname")) or name
    args = xml_text(member.find("argsstring"))
    template = template_signature(member)
    type_text = xml_text(member.find("type"))

    if member_kind in {"function", "friend"}:
        kind = classify_function(compound_kind, compound_name, name)
        signature = normalize_space(template + args)
        display = normalize_space(f"{template}{qualified_name}{args}")
    elif member_kind == "variable":
        if "EventHandler" in type_text:
            kind = "event"
        elif member.get("static") == "yes" and (
            "const" in type_text.split() or "constexpr" in type_text.split()
        ):
            kind = "constant"
        else:
            kind = "field"
        signature = ""
        display = qualified_name
        initializer = xml_text(member.find("initializer"))
        if initializer:
            display = f"{display} {initializer}"
    elif member_kind == "enum":
        kind = "enum"
        signature = ""
        display = qualified_name
    elif member_kind == "typedef":
        kind = "alias"
        signature = normalize_space(template + type_text)
        display = normalize_space(f"{qualified_name} = {type_text}")
    elif member_kind == "define":
        kind = "macro"
        signature = args
        display = normalize_space(f"{qualified_name}{args}")
    else:
        kind = member_kind
        signature = args
        display = normalize_space(f"{qualified_name}{args}")

    return Symbol(
        header=header,
        line=line,
        kind=kind,
        qualified_name=qualified_name,
        signature=signature,
        display=display,
        access=member.get("prot") or "public",
        type_text=type_text,
    )


def enum_value_symbols(root: Path, enum_symbol: Symbol, member: ET.Element) -> Iterable[Symbol]:
    for value in member.findall("enumvalue"):
        location = normalize_location(root, value.find("location"))
        header, line = location if location is not None else (enum_symbol.header, enum_symbol.line)
        name = xml_text(value.find("name"))
        qualified_name = f"{enum_symbol.qualified_name}::{name}"
        initializer = xml_text(value.find("initializer"))
        display = qualified_name + (f" {initializer}" if initializer else "")
        yield Symbol(
            header=header,
            line=line,
            kind="enum-value",
            qualified_name=qualified_name,
            signature="",
            display=display,
            access=enum_symbol.access,
            type_text=enum_symbol.qualified_name,
        )


def parse_symbols(root: Path, xml_directory: Path, headers: list[Path]) -> list[Symbol]:
    header_names = {path.as_posix() for path in headers}
    symbols: list[Symbol] = []
    for xml_file in sorted(xml_directory.glob("*.xml")):
        try:
            compound = ET.parse(xml_file).getroot().find("compounddef")
        except ET.ParseError as error:
            raise RuntimeError(f"Invalid Doxygen XML {xml_file}: {error}") from error
        if compound is None:
            continue
        compound_kind = compound.get("kind", "")
        compound_name = xml_text(compound.find("compoundname"))
        compound_access = compound.get("prot")

        if compound_kind in COMPOUND_KINDS and compound_access in PUBLIC_ACCESS:
            location = normalize_location(root, compound.find("location"))
            if location is not None and location[0] in header_names:
                template = template_signature(compound)
                symbols.append(
                    Symbol(
                        header=location[0],
                        line=location[1],
                        kind=compound_kind,
                        qualified_name=compound_name,
                        signature=normalize_space(template),
                        display=normalize_space(template + compound_name),
                        access=compound_access or "public",
                        type_text=compound_kind,
                    )
                )

        if compound_kind not in MEMBER_COMPOUND_KINDS or compound_access == "private":
            continue
        for member in compound.findall("./sectiondef/memberdef"):
            if compound_kind == "file" and member.get("kind") != "define":
                continue
            symbol = symbol_from_member(root, compound_kind, compound_name, member)
            if symbol is None or symbol.header not in header_names:
                continue
            symbols.append(symbol)
            if symbol.kind == "enum":
                symbols.extend(enum_value_symbols(root, symbol, member))

    unique: dict[str, Symbol] = {}
    for symbol in sorted(symbols, key=lambda value: (value.header, value.line, value.identity)):
        existing = unique.get(symbol.identity)
        if existing is None or symbol.line < existing.line:
            unique[symbol.identity] = symbol

    stable_ids: dict[str, str] = {}
    for symbol in unique.values():
        previous = stable_ids.setdefault(symbol.stable_id, symbol.identity)
        if previous != symbol.identity:
            raise RuntimeError(
                f"Stable symbol ID collision {symbol.stable_id}: {previous!r} vs {symbol.identity!r}"
            )
    return sorted(unique.values(), key=lambda value: (value.header, value.line, value.identity))


def load_rules(path: Path) -> list[Rule]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("schema_version") != SCHEMA_VERSION:
        raise RuntimeError(f"Unsupported coverage mapping schema in {path}.")
    rules: list[Rule] = []
    seen_ids: set[str] = set()
    for raw in payload.get("rules", []):
        rule_id = raw["id"]
        if rule_id in seen_ids:
            raise RuntimeError(f"Duplicate mapping rule id: {rule_id}")
        seen_ids.add(rule_id)
        status = raw["status"]
        # ``not-applicable`` is also reachable without a rule, for a declaration the C++ source
        # explicitly deletes.  An explicit rule additionally records a reviewed declaration that
        # exposes no callable public behavior at all, such as a friendship declaration.
        if status not in {"implemented", "partial", "not-applicable"}:
            raise RuntimeError(f"Explicit rule {rule_id} has invalid status {status!r}.")
        # An empty list is how a newly written rule starts: it covers nothing until someone runs
        # --approve-rule-symbols for it, and until then its symbols stay `planned`. A missing key
        # is still an error, so the field cannot be forgotten.
        approved = raw.get("approved_symbols")
        if not isinstance(approved, list):
            raise RuntimeError(
                f"Explicit rule {rule_id} has no approved_symbols. Every rule names the stable IDs "
                "it was reviewed against; run --approve-rule-symbols to record them deliberately."
            )
        for stable_id in approved:
            if not isinstance(stable_id, str) or not STABLE_ID_PATTERN.fullmatch(stable_id):
                raise RuntimeError(
                    f"Explicit rule {rule_id} has invalid approved symbol {stable_id!r}."
                )
        if len(set(approved)) != len(approved):
            raise RuntimeError(f"Explicit rule {rule_id} lists a duplicate approved symbol.")
        rules.append(
            Rule(
                rule_id=rule_id,
                qualified_name=re.compile(raw["qualified_name_regex"]),
                signature=re.compile(raw["signature_regex"]) if raw.get("signature_regex") else None,
                header=re.compile(raw["header_regex"]) if raw.get("header_regex") else None,
                kinds=frozenset(raw.get("kinds", [])),
                mapping=raw["mapping"],
                tests=raw["tests"],
                status=status,
                task=raw["task"],
                approved_symbols=frozenset(approved),
            )
        )
    return rules


def approve_rule_symbols(
    path: Path,
    usage: dict[str, list[str]],
    only: list[str] | None = None,
) -> int:
    """Record the symbols each explicit rule is reviewed against.

    Deliberately separate from ``--write``: the routine command a developer runs to refresh the
    inventory must not be able to extend a rule's authority, because a broad rule asserts that a
    *reviewed* set of declarations is bound and tested.  Running this says the newly matched
    symbols have been looked at and the rule's mapping and test evidence genuinely cover them.
    """
    payload = json.loads(path.read_text(encoding="utf-8"))
    selected = set(only or ())
    known = {raw["id"] for raw in payload.get("rules", [])}
    if unknown := sorted(selected - known):
        raise RuntimeError("No such coverage rule: " + ", ".join(unknown))
    changed: list[str] = []
    for raw in payload.get("rules", []):
        rule_id = raw["id"]
        if selected and rule_id not in selected:
            continue
        measured = sorted(usage.get(rule_id, ()))
        previous = raw.get("approved_symbols") or []
        if sorted(previous) != measured:
            added = len(set(measured) - set(previous))
            removed = len(set(previous) - set(measured))
            changed.append(f"{rule_id}: {len(previous)} -> {len(measured)} (+{added}/-{removed})")
            raw["approved_symbols"] = measured
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    if not changed:
        print(f"Approved coverage rule symbols already current in {path.name}")
        return 0
    print(f"Re-approved {len(changed)} coverage rules in {path.name}:")
    for line in changed:
        print(f"  {line}")
    return 0


# CBIND-079, 2026-08-26: the CNAEXT engine layer and a post-merge tail of ordinary XNA symbols
# reopened the coverage matrix, and every row they left behind still named `CBIND-035` -- a task
# plans/plan_binding.md records as complete. A finished task cannot own unfinished work: a matrix
# that says so is how 1,414 unmapped symbols hid behind a green tick for a week. The reopened rows
# are partitioned here onto the Phase B9 slices that actually own them, keyed by module and header
# stem so the assignment is greppable from either direction. `validate_planned_row_owners` below
# turns the invariant this restores -- no completed task owns a planned row -- into a gate that
# fails rather than a convention that erodes.
CNAEXT_SLICE_OWNERS: dict[str, str] = {
    # CBIND-080 -- 15 rows
    "graphics/DynamicIndexBuffer": "CBIND-080",
    "graphics/DynamicVertexBuffer": "CBIND-080",
    "graphics/Effect": "CBIND-080",
    "graphics/RenderTarget2D": "CBIND-080",
    "graphics/RenderTargetCube": "CBIND-080",
    "graphics/SpriteBatch": "CBIND-080",
    "graphics/TextureCube": "CBIND-080",
    "graphics/VertexBuffer": "CBIND-080",
    # CBIND-081 -- 7 rows
    "math/Color": "CBIND-081",
    "math/Vector2": "CBIND-081",
    # CBIND-082 -- 23 rows
    "media/Album": "CBIND-082",
    "media/Artist": "CBIND-082",
    "media/Genre": "CBIND-082",
    "media/MediaSource": "CBIND-082",
    "media/Picture": "CBIND-082",
    "media/PictureAlbum": "CBIND-082",
    "media/Playlist": "CBIND-082",
    "media/Song": "CBIND-082",
    "media/VideoPlayer": "CBIND-082",
    # CBIND-083 -- 5 rows
    "content/ContentManager": "CBIND-083",
    "input/TouchPanel": "CBIND-083",
    "runtime/DrawableGameComponent": "CBIND-083",
    # CBIND-084 -- 121 rows
    "graphics-ext/BlitPass": "CBIND-084",
    "graphics-ext/ComputeShader": "CBIND-084",
    "graphics-ext/DepthEncoding": "CBIND-084",
    "graphics-ext/EffectPass": "CBIND-084",
    "graphics-ext/EngineException": "CBIND-084",
    "graphics-ext/EngineLayerVersion": "CBIND-084",
    "graphics-ext/FullscreenPass": "CBIND-084",
    "graphics-ext/GpuTimer": "CBIND-084",
    "graphics-ext/MaterialBinding": "CBIND-084",
    "graphics-ext/PostProcessContext": "CBIND-084",
    "graphics-ext/PostProcessPass": "CBIND-084",
    "graphics-ext/RenderTargetPool": "CBIND-084",
    "graphics-ext/RequireCapability": "CBIND-084",
    "graphics-ext/ScopedRenderTarget": "CBIND-084",
    "graphics-ext/ShaderDiagnostics": "CBIND-084",
    "graphics-ext/ShaderEffectFactory": "CBIND-084",
    "graphics-ext/StorageBuffer": "CBIND-084",
    # CBIND-085 -- 209 rows
    "graphics-ext/CascadedShadowMap": "CBIND-085",
    "graphics-ext/ClusteredShadowPolicyEXT": "CBIND-085",
    "graphics-ext/ContactShadowPass": "CBIND-085",
    "graphics-ext/CubeShadowMap": "CBIND-085",
    "graphics-ext/DepthNormalPrepass": "CBIND-085",
    "graphics-ext/DirectionalLightEXT": "CBIND-085",
    "graphics-ext/PointLightEXT": "CBIND-085",
    "graphics-ext/ShadowMap": "CBIND-085",
    "graphics-ext/SpotLightEXT": "CBIND-085",
    "graphics-ext/SpotShadowMap": "CBIND-085",
    "graphics/IShadowReceiverEXT": "CBIND-085",
    "graphics/PunctualLightEXT": "CBIND-085",
    "graphics/ShadowCascadeStateEXT": "CBIND-085",
    # CBIND-086 -- 116 rows
    "graphics-ext/ClusteredForwardEffect": "CBIND-086",
    "graphics-ext/ClusteredLightAssignment": "CBIND-086",
    "graphics-ext/ClusteredLightBuffer": "CBIND-086",
    "graphics-ext/ClusteredLightCompute": "CBIND-086",
    "graphics-ext/ClusteredLightEXT": "CBIND-086",
    "graphics-ext/ClusteredLightGrid": "CBIND-086",
    "graphics-ext/ClusteredLightSetEXT": "CBIND-086",
    "graphics-ext/ClusteredLightType": "CBIND-086",
    # CBIND-087 -- 177 rows
    "graphics-ext/GltfMaterialBridge": "CBIND-087",
    "graphics-ext/PbrMaterial": "CBIND-087",
    "graphics-ext/PbrMaterialExtensions": "CBIND-087",
    "graphics-ext/ThinFilmIridescence": "CBIND-087",
    "graphics-ext/TransparencyMode": "CBIND-087",
    "graphics-ext/TransparentDrawList": "CBIND-087",
    "graphics-ext/WeightedBlendedTransparency": "CBIND-087",
    "graphics/PbrEffect": "CBIND-087",
    "graphics/SkinnedPbrEffect": "CBIND-087",
    # CBIND-088 -- 117 rows
    "graphics-ext/RenderPipeline": "CBIND-088",
    "graphics-ext/RenderPipelineSettings": "CBIND-088",
    # CBIND-089 -- 232 rows
    "graphics-ext/AerialPerspectivePass": "CBIND-089",
    "graphics-ext/AsciiPass": "CBIND-089",
    "graphics-ext/BloomPass": "CBIND-089",
    "graphics-ext/ChromaticAberrationPass": "CBIND-089",
    "graphics-ext/DecalPass": "CBIND-089",
    "graphics-ext/DepthOfFieldPass": "CBIND-089",
    "graphics-ext/FilmGrainPass": "CBIND-089",
    "graphics-ext/FxaaPass": "CBIND-089",
    "graphics-ext/HeightFogPass": "CBIND-089",
    "graphics-ext/LensFlarePass": "CBIND-089",
    "graphics-ext/LightShaftPass": "CBIND-089",
    "graphics-ext/MotionBlurPass": "CBIND-089",
    "graphics-ext/PostProcessChain": "CBIND-089",
    "graphics-ext/SpatialUpscalePass": "CBIND-089",
    "graphics-ext/SsaoPass": "CBIND-089",
    "graphics-ext/SsrPass": "CBIND-089",
    "graphics-ext/VolumetricFogPass": "CBIND-089",
    # CBIND-090 -- 87 rows
    "graphics-ext/AutoExposureEXT": "CBIND-090",
    "graphics-ext/ColorGradePass": "CBIND-090",
    "graphics-ext/CubeLut": "CBIND-090",
    "graphics-ext/HdrDisplayOutput": "CBIND-090",
    "graphics-ext/LutInterpolation": "CBIND-090",
    "graphics-ext/TonemapPass": "CBIND-090",
    "graphics-ext/TonemappingMode": "CBIND-090",
    "graphics/DisplayColorSpace": "CBIND-090",
    # CBIND-091 -- 137 rows
    "graphics-ext/AreaLightBrdfTable": "CBIND-091",
    "graphics-ext/AreaLightShading": "CBIND-091",
    "graphics-ext/AtmosphericSky": "CBIND-091",
    "graphics-ext/EnvironmentProcessor": "CBIND-091",
    "graphics-ext/LightProbeBaker": "CBIND-091",
    "graphics-ext/LightProbeEXT": "CBIND-091",
    "graphics-ext/LightProbeVolumeEXT": "CBIND-091",
    "graphics-ext/Skybox": "CBIND-091",
    "graphics/AreaLightEXT": "CBIND-091",
    "graphics/ImageBasedLightEXT": "CBIND-091",
    # CBIND-092 -- 157 rows
    "graphics-ext/DebugDraw": "CBIND-092",
    "graphics-ext/DebugGizmos": "CBIND-092",
    "graphics-ext/FrustumCullerEXT": "CBIND-092",
    "graphics-ext/GpuInstanceCuller": "CBIND-092",
    "graphics-ext/InstancedRendererEXT": "CBIND-092",
    "graphics-ext/LodGroupEXT": "CBIND-092",
    "graphics-ext/ParticleSystem": "CBIND-092",
    "graphics/GraphicsImageAccess": "CBIND-092",
    "graphics/GraphicsMemoryBarrier": "CBIND-092",
    "graphics/IndirectDrawArguments": "CBIND-092",
    # CBIND-093 -- 48 rows
    "core/AssemblyInfo": "CBIND-093",
    "graphics/BasicEffect": "CBIND-093",
    "graphics/GraphicsDevice": "CBIND-093",
    "graphics/ShaderEffect": "CBIND-093",
    "graphics/SkinnedEffect": "CBIND-093",
}


# CBIND-084C, 2026-08-26. One symbol whose owner is not its header's slice. PostProcessContext's
# `settings` field points at a RenderPipelineSettings, and the C form of that type is still a
# subset of the canonical one; binding the field now would silently apply engine defaults for
# every field the subset omits. The field therefore waits for CBIND-088, which owns the settings
# type, while the rest of the struct is bound by CBIND-084C.
SYMBOL_OWNER_OVERRIDES: dict[str, str] = {
    "CNA::Graphics::PostProcessContext::settings": "CBIND-088",
    # CBIND-086C: the forward effect's material and light-probe setters each take a type another
    # slice owns, so they wait for that slice rather than for the one that binds the effect. The
    # effect itself is bound; only the argument is missing.
    "CNA::Graphics::ClusteredForwardEffect::setMaterialExtensions": "CBIND-087",
    "CNA::Graphics::ClusteredForwardEffect::getMaterialExtensions": "CBIND-087",
    "CNA::Graphics::ClusteredForwardEffect::setAreaLight": "CBIND-091",
    "CNA::Graphics::ClusteredForwardEffect::setLightProbe": "CBIND-091",
    "CNA::Graphics::ClusteredForwardEffect::setLightProbeVolume": "CBIND-091",
}


def owner_task(symbol: Symbol) -> str:
    override = SYMBOL_OWNER_OVERRIDES.get(symbol.qualified_name)
    if override is not None:
        return override
    header = Path(symbol.header)
    slice_owner = CNAEXT_SLICE_OWNERS.get(f"{header.parts[1]}/{header.stem}")
    if slice_owner is not None:
        return slice_owner
    module = header.parts[1]
    if module == "graphics":
        if re.search(
            r"(RenderTarget|SpriteFont|BlendState|SamplerState|DepthStencilState|RasterizerState|"
            r"PresentationParameters|DisplayMode|GraphicsAdapter)",
            symbol.qualified_name,
        ):
            return "CBIND-034"
        return "CBIND-035"
    if module in {"math", "graphics-ext"}:
        return "CBIND-035"
    if module in {"storage", "net", "content"}:
        return "CBIND-036"
    if module in {
        "runtime",
        "devices",
        "devices-ext",
        "input",
        "audio",
        "media",
        "gamer-services",
    }:
        return "CBIND-037"
    return "CBIND-044"


def planned_mapping(symbol: Symbol, task: str) -> str:
    if symbol.kind in COMPOUND_KINDS:
        representation = "`CNA_*` POD or validated handle design"
    elif symbol.kind == "enum":
        representation = "fixed-width `CNA_*` identity design"
    elif symbol.kind == "enum-value":
        representation = "stable `CNA_*` constant"
    elif symbol.kind == "constructor":
        representation = "`cna_*_create_<variant>` operation"
    elif symbol.kind == "destructor":
        representation = "`cna_*_destroy`/release operation"
    elif symbol.kind == "event":
        representation = "C callback registration and context"
    elif symbol.kind == "field":
        representation = "POD field or `cna_*_get/set_*` access"
    elif symbol.kind == "constant":
        representation = "stable `CNA_*` constant"
    elif symbol.kind == "alias":
        representation = "fixed-width/POD C alias"
    elif symbol.kind == "operator":
        representation = "named `cna_*` value operation"
    elif symbol.kind == "macro":
        representation = "C build/integration equivalent"
    else:
        representation = "`cna_*` operation with C-safe arguments"
    return f"Planned {representation} ({task})"


def map_symbols(
    symbols: list[Symbol],
    rules: list[Rule],
    *,
    ignore_approval: bool = False,
    usage_out: dict[str, list[str]] | None = None,
) -> dict[str, Mapping]:
    result: dict[str, Mapping] = {}
    usage: Counter[str] = Counter()
    matched: defaultdict[str, list[str]] = defaultdict(list)
    for symbol in symbols:
        if symbol.signature.endswith("=delete"):
            result[symbol.identity] = Mapping(
                mapping="No C mapping: operation is explicitly deleted in the public C++ declaration",
                tests="Doxygen declaration inventory; no callable native operation exists",
                status="not-applicable",
                task="CBIND-033",
                rule_id=None,
            )
            continue
        matches = [
            rule for rule in rules if rule.matches(symbol, ignore_approval=ignore_approval)
        ]
        if len(matches) > 1:
            raise RuntimeError(
                f"Ambiguous explicit mappings for {symbol.identity}: "
                + ", ".join(rule.rule_id for rule in matches)
            )
        if matches:
            rule = matches[0]
            usage[rule.rule_id] += 1
            matched[rule.rule_id].append(symbol.stable_id)
            result[symbol.identity] = Mapping(
                mapping=rule.mapping,
                tests=rule.tests,
                status=rule.status,
                task=rule.task,
                rule_id=rule.rule_id,
            )
            continue
        task = owner_task(symbol)
        result[symbol.identity] = Mapping(
            mapping=planned_mapping(symbol, task),
            tests=f"Pending C-only behavior/lifetime/ABI evidence ({task})",
            status="planned",
            task=task,
            rule_id=None,
        )

    if usage_out is not None:
        usage_out.clear()
        usage_out.update({rule.rule_id: matched[rule.rule_id] for rule in rules})

    # A rule that speaks for nothing is dead weight, and after approval pinning it also means the
    # declarations it was reviewed against are gone -- so the C routes it names are now orphaned.
    unused = [rule.rule_id for rule in rules if usage[rule.rule_id] == 0]
    if unused:
        raise RuntimeError("Explicit coverage rules matched no symbols: " + ", ".join(unused))
    return result


def markdown_escape(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ")


def render_markdown(
    headers: list[Path],
    excluded: list[Path],
    symbols: list[Symbol],
    mappings: dict[str, Mapping],
) -> str:
    by_header: dict[str, list[Symbol]] = defaultdict(list)
    for symbol in symbols:
        by_header[symbol.header].append(symbol)

    status_counts = Counter(mappings[symbol.identity].status for symbol in symbols)
    module_headers: Counter[str] = Counter()
    module_symbols: Counter[str] = Counter()
    module_statuses: dict[str, Counter[str]] = defaultdict(Counter)
    for header in headers:
        module_headers[header.parts[1]] += 1
    for symbol in symbols:
        module = Path(symbol.header).parts[1]
        module_symbols[module] += 1
        module_statuses[module][mappings[symbol.identity].status] += 1

    lines = [
        "# CNA C API Coverage Matrix",
        "",
        "<!-- Generated by tools/c-api/generate_coverage_inventory.py; do not edit by hand. -->",
        "",
        "## Coverage contract",
        "",
        "This file is the complete reviewed CBIND-033 inventory of CNA's public C++ declaration",
        "surface. It is generated from every `modules/*/include/Microsoft/**/*.hpp` and",
        "`modules/*/include/CNA/**/*.hpp` header. Paths containing the explicit implementation",
        "segments `Internal` or `Detail`, the whole `modules/platform` module, and the C API's own",
        "`.h` headers, are excluded. The platform module is excluded as a **substrate**: the C ABI is",
        "built on it and a C caller reaches platform behaviour through the routes that use it, never",
        "through `IPlatform` -- an owner decision of 2026-08-16, recorded as `CBIND-047`. A",
        "header remains listed even when it declares no public/protected symbol.",
        "",
        "Every symbol below has a stable content-derived `CPP-*` ID, a C-native mapping, required",
        "test evidence and a status. `implemented` means an explicit rule names existing C API",
        "and tests; `partial` means the named C route covers only the stated specialization or",
        "subset; `planned` is not coverage and names the task that owns its design; and",
        "`not-applicable` records an explicitly deleted operation that has no callable source",
        "behavior to expose. Nothing is treated as implemented merely because a related C",
        "operation exists.",
        "",
        f"Snapshot: **{len(headers)} headers**, **{len(symbols)} symbols**, "
        f"**{status_counts['implemented']} implemented**, **{status_counts['partial']} partial**, "
        f"**{status_counts['planned']} planned**, **{status_counts['not-applicable']} not applicable**. "
        "Explicitly excluded internal/detail headers: "
        f"**{len(excluded)}**.",
        "",
        "Regenerate with:",
        "",
        "```bash",
        "python3 tools/c-api/generate_coverage_inventory.py --write",
        "python3 tools/c-api/generate_coverage_inventory.py --check",
        "```",
        "",
        "`--check` is a mandatory gate (CBIND-043), enforced in two places: the CTest test",
        "`CApiCoverageMatrix`, registered under `CNA_BUILD_TESTS`, and the build-free",
        "`.github/workflows/c-api-coverage-gate.yml` workflow, which runs on every push.",
        "",
        "## Module summary",
        "",
        "| Module | Headers | Symbols | Implemented | Partial | Planned | N/A |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for module in sorted(module_headers):
        counts = module_statuses[module]
        lines.append(
            f"| `{module}` | {module_headers[module]} | {module_symbols[module]} | "
            f"{counts['implemented']} | {counts['partial']} | {counts['planned']} | "
            f"{counts['not-applicable']} |"
        )

    lines.extend(
        (
            "",
            "## Status definitions",
            "",
            "| Status | Meaning |",
            "|---|---|",
            "| ✅ implemented | Exact current C mapping and C-only evidence are named. |",
            "| 🟨 partial | A useful specialization/subset exists, but the source symbol is not fully mapped. |",
            "| ⬜ planned | No complete C mapping exists; the owner task and required evidence are explicit. |",
            "| ➖ not applicable | The public declaration explicitly deletes this operation, so no callable C++ behavior exists to map. |",
            "",
            "## Complete public symbol inventory",
            "",
        )
    )

    current_module = None
    status_label = {
        "implemented": "✅ implemented",
        "partial": "🟨 partial",
        "planned": "⬜ planned",
        "not-applicable": "➖ not applicable",
    }
    for header_path in headers:
        header = header_path.as_posix()
        module = header_path.parts[1]
        if module != current_module:
            lines.extend((f"### Module `{module}`", ""))
            current_module = module
        lines.extend(
            (
                f"#### `{header}`",
                "",
                "| ID | Line | Access/kind | C++ symbol | C mapping | Tests | Status |",
                "|---|---:|---|---|---|---|---|",
            )
        )
        header_symbols = by_header.get(header, [])
        if not header_symbols:
            lines.append(
                "| — | — | — | *(no public/protected declaration in this header)* | — | Header tracked | — |"
            )
        for symbol in header_symbols:
            mapping = mappings[symbol.identity]
            access_kind = f"{symbol.access} {symbol.kind}"
            lines.append(
                f"| `{symbol.stable_id}` | {symbol.line} | {markdown_escape(access_kind)} | "
                f"`{markdown_escape(symbol.display)}` | {markdown_escape(mapping.mapping)} | "
                f"{markdown_escape(mapping.tests)} | {status_label[mapping.status]} (`{mapping.task}`) |"
            )
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


PLAN_PATH = Path("plans/plan_binding.md")
PLAN_STATUS_MARKS = {"\u2705": "complete", "\U0001f7e8": "in progress", "\u2b1c": "not started"}


def parse_plan_task_status(root: Path) -> dict[str, str]:
    """Read every `| CBIND-… | … | <mark> | … |` row in the plan and return id -> status.

    The plan's task tables do not all carry the same columns -- some phases add a row-count
    column between the subject and the status -- so the status is found by looking for the one
    cell that is exactly a status mark rather than by counting columns.
    """
    statuses: dict[str, str] = {}
    plan = root / PLAN_PATH
    if not plan.is_file():
        return statuses
    for line in plan.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| CBIND-"):
            continue
        cells = [cell.strip() for cell in line.split("|")]
        task = cells[1]
        marks = [cell for cell in cells[2:] if cell in PLAN_STATUS_MARKS]
        if len(marks) != 1:
            continue
        statuses[task] = PLAN_STATUS_MARKS[marks[0]]
    return statuses


# CBIND-079, 2026-08-26. The invariant this enforces is narrow and specific: a task the plan
# records as complete may not own a `planned` row. It is not "no planned row may exist" -- Phase
# B9 opens 1,451 of them on purpose, and a gate that forbade them would simply be switched off.
#
# The state it forbids is the one that actually happened. `CBIND-035` closed, four merges grew the
# tracked surface, `owner_task()` kept attributing the new unmapped symbols to it by module, and
# for a week COVERAGE.md asserted that a finished task owned 1,414 unfinished symbols while
# `## Current status` read "0 planned" and RELEASE_GATE.md read "Not ready". Nothing was lying;
# three documents were each locally consistent and nobody re-read them together. This makes the
# combination fail instead.
def validate_planned_row_owners(root: Path, mappings: dict[str, Mapping]) -> None:
    statuses = parse_plan_task_status(root)
    if not statuses:
        return
    finished: Counter[str] = Counter()
    unknown: Counter[str] = Counter()
    for mapping in mappings.values():
        if mapping.status != "planned":
            continue
        status = statuses.get(mapping.task)
        if status is None:
            unknown[mapping.task] += 1
        elif status == "complete":
            finished[mapping.task] += 1
    problems: list[str] = []
    for task, count in sorted(finished.items()):
        problems.append(
            f"  {task} is recorded complete in {PLAN_PATH} but owns {count} planned row(s)"
        )
    for task, count in sorted(unknown.items()):
        problems.append(f"  {task} owns {count} planned row(s) but has no row in {PLAN_PATH}")
    if problems:
        raise RuntimeError(
            "A planned row names a task that cannot own it. Unfinished work must name an "
            "unfinished task: either open a task for these symbols and point owner_task() at "
            "it, or map them.\n" + "\n".join(problems)
        )


def validate_inventory(
    headers: list[Path],
    excluded: list[Path],
    symbols: list[Symbol],
    mappings: dict[str, Mapping],
) -> None:
    header_names = {path.as_posix() for path in headers}
    excluded_names = {path.as_posix() for path in excluded}
    if header_names & excluded_names:
        raise RuntimeError("A header is both included and excluded from the public inventory.")
    for header in header_names:
        relative_to_include = Path(header.split("/include/", 1)[1])
        if path_is_explicitly_internal(relative_to_include):
            raise RuntimeError(f"Explicitly internal path entered the public inventory: {header}")
    if set(mappings) != {symbol.identity for symbol in symbols}:
        raise RuntimeError("Not every source symbol received exactly one C mapping/status record.")
    if any(not value.mapping or not value.tests or not value.status for value in mappings.values()):
        raise RuntimeError("A source symbol has an incomplete mapping/test/status record.")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="regenerate docs/c-api/COVERAGE.md")
    mode.add_argument("--check", action="store_true", help="fail if the checked-in inventory is stale")
    mode.add_argument(
        "--approve-rule-symbols",
        action="store_true",
        help=(
            "record the symbols each explicit rule is reviewed against, after the newly matched "
            "ones have actually been bound or dispositioned"
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="override the Markdown path (primarily for isolated generator tests)",
    )
    parser.add_argument(
        "--rule",
        action="append",
        default=[],
        metavar="ID",
        help=(
            "with --approve-rule-symbols, re-approve only these rules; repeatable. A task that "
            "binds one family must not approve the rest along with it"
        ),
    )
    arguments = parser.parse_args()
    if arguments.rule and not arguments.approve_rule_symbols:
        parser.error("--rule is only meaningful with --approve-rule-symbols")
    return arguments


def main() -> int:
    arguments = parse_arguments()
    root = repository_root()
    output = arguments.output or root / "docs" / "c-api" / "COVERAGE.md"
    if not output.is_absolute():
        output = root / output
    rules_path = root / "tools" / "c-api" / "coverage_mappings.json"

    headers, excluded = discover_headers(root)
    rules = load_rules(rules_path)
    with tempfile.TemporaryDirectory(prefix="cna-c-api-coverage-") as temporary:
        xml_directory = run_doxygen(root, headers, Path(temporary))
        symbols = parse_symbols(root, xml_directory, headers)
    usage: dict[str, list[str]] = {}
    mappings = map_symbols(
        symbols,
        rules,
        ignore_approval=arguments.approve_rule_symbols,
        usage_out=usage,
    )
    if arguments.approve_rule_symbols:
        return approve_rule_symbols(rules_path, usage, arguments.rule)
    validate_inventory(headers, excluded, symbols, mappings)
    validate_planned_row_owners(root, mappings)
    rendered = render_markdown(headers, excluded, symbols, mappings)

    summary = Counter(mapping.status for mapping in mappings.values())
    summary_text = (
        f"CNA C API coverage inventory: {len(headers)} headers, {len(symbols)} symbols; "
        + ", ".join(f"{status}={summary[status]}" for status in STATUS_ORDER)
    )
    if arguments.write:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
        print(f"{summary_text}; wrote {output.relative_to(root)}")
        return 0

    if not output.is_file():
        print(f"Coverage inventory is missing: {output}", file=sys.stderr)
        return 1
    existing = output.read_text(encoding="utf-8")
    if existing != rendered:
        print(
            f"Coverage inventory is stale: run {Path(__file__).relative_to(root)} --write",
            file=sys.stderr,
        )
        return 1
    print(f"{summary_text}; checked-in inventory is current")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"coverage inventory error: {error}", file=sys.stderr)
        raise SystemExit(2) from error
