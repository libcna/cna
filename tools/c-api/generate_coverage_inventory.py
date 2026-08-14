#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL

"""Generate the complete public C++ to C API coverage inventory.

The scanner deliberately uses Doxygen's C++ parser instead of a collection of regular
expressions.  Doxygen expands CNA's SharpRuntime property macros, preserves overload signatures,
reports access levels and emits stable source locations.  The generated Markdown remains a
reviewable snapshot; CBIND-043 is responsible for promoting ``--check`` into a mandatory CI gate.
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
COMPOUND_KINDS = {"class", "struct", "union"}
MEMBER_COMPOUND_KINDS = COMPOUND_KINDS | {"namespace", "file"}
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

    def matches(self, symbol: Symbol) -> bool:
        return (
            (not self.kinds or symbol.kind in self.kinds)
            and self.qualified_name.fullmatch(symbol.qualified_name) is not None
            and (self.signature is None or self.signature.fullmatch(symbol.signature) is not None)
            and (self.header is None or self.header.fullmatch(symbol.header) is not None)
        )


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
        if status not in {"implemented", "partial"}:
            raise RuntimeError(f"Explicit rule {rule_id} has invalid status {status!r}.")
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
            )
        )
    return rules


def owner_task(symbol: Symbol) -> str:
    module = Path(symbol.header).parts[1]
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


def map_symbols(symbols: list[Symbol], rules: list[Rule]) -> dict[str, Mapping]:
    result: dict[str, Mapping] = {}
    usage: Counter[str] = Counter()
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
        matches = [rule for rule in rules if rule.matches(symbol)]
        if len(matches) > 1:
            raise RuntimeError(
                f"Ambiguous explicit mappings for {symbol.identity}: "
                + ", ".join(rule.rule_id for rule in matches)
            )
        if matches:
            rule = matches[0]
            usage[rule.rule_id] += 1
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
        "segments `Internal` or `Detail`, and the C API's own `.h` headers, are excluded. A",
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
        "`--check` is available now for deterministic review. Making it a mandatory configured/CI",
        "gate is deliberately reserved for CBIND-043.",
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
    parser.add_argument(
        "--output",
        type=Path,
        help="override the Markdown path (primarily for isolated generator tests)",
    )
    return parser.parse_args()


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
    mappings = map_symbols(symbols, rules)
    validate_inventory(headers, excluded, symbols, mappings)
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
