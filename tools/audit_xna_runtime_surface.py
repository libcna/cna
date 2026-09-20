#!/usr/bin/env python3
"""Audit documented XNA 4.0 runtime types against CNA public headers.

The source corpus is Microsoft's XNA 4.0 XML documentation, not FNA. The
Content.Pipeline assembly is a build-time tool and is excluded. All runtime
T: entries are counted, including nested and generic types. Member shapes
come from matching Microsoft DLL metadata and CNA public Clang ASTs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
DEFAULT_XML = REPO.parent / "xna4-decomp" / "dlls"
RUNTIME_EXCLUSION = "Microsoft.Xna.Framework.Content.Pipeline.xml"
RUNTIME_XML_FILES = (
    "Microsoft.Xna.Framework.xml",
    "Microsoft.Xna.Framework.Avatar.xml",
    "Microsoft.Xna.Framework.Game.xml",
    "Microsoft.Xna.Framework.GamerServices.xml",
    "Microsoft.Xna.Framework.Graphics.xml",
    "Microsoft.Xna.Framework.Input.Touch.xml",
    "Microsoft.Xna.Framework.Net.xml",
    "Microsoft.Xna.Framework.Storage.xml",
    "Microsoft.Xna.Framework.Video.xml",
    "Microsoft.Xna.Framework.Xact.xml",
)
ARITY_SUBSTITUTIONS = {
    "Microsoft.Xna.Framework.Content.ContentTypeReader": "ContentTypeReaderBase",
    "Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector`1": "IPackedVectorT",
}
TYPE_PATTERN = re.compile(
    r"\b(?:class|struct|enum(?:\s+class)?)\s+(?:CNAEXT\s+)?([A-Za-z_]\w*)\b"
)
COMMENT_PATTERN = re.compile(r"//[^\n]*|/\*[\s\S]*?\*/")
STRING_PATTERN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
ACCESS_PATTERN = re.compile(r"\b(public|protected|private)\s*:")


def stripped(source: str) -> str:
    """Remove comments and literals while preserving brace positions."""
    source = COMMENT_PATTERN.sub(lambda match: " " * len(match.group()), source)
    return STRING_PATTERN.sub(lambda match: " " * len(match.group()), source)


def named_body(source: str, name: str) -> str | None:
    """Find a definition, rather than a forward declaration, of a type."""
    for match in TYPE_PATTERN.finditer(source):
        if match.group(1) != name:
            continue
        begin = source.find("{", match.end())
        semicolon = source.find(";", match.end())
        if begin < 0 or (semicolon >= 0 and semicolon < begin):
            continue
        depth = 1
        for index in range(begin + 1, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    return source[begin + 1 : index]
    return None


def public_nested_body(owner_body: str, child: str, owner_kind: str) -> bool:
    """Require a concrete child declaration in the owner's public section."""
    if named_body(owner_body, child) is None:
        return False
    events = ([(match.start(), "access", match.group(1))
               for match in ACCESS_PATTERN.finditer(owner_body)]
              + [(match.start(), "type", match.group(1))
                 for match in TYPE_PATTERN.finditer(owner_body)])
    depth = 0
    previous = 0
    access = "public" if owner_kind == "struct" else "private"
    for position, kind, value in sorted(events):
        for character in owner_body[previous:position]:
            if character == "{":
                depth += 1
            elif character == "}":
                depth -= 1
        previous = position
        if depth == 0:
            if kind == "access":
                access = value
            elif value == child:
                return access == "public"
    return False


def normalized_segments(reference_name: str) -> tuple[list[str], bool]:
    """Map CLR arity to the established C++ names without merging CLR types."""
    segments = reference_name.split(".")
    substitution = reference_name in ARITY_SUBSTITUTIONS
    if substitution:
        segments[-1] = ARITY_SUBSTITUTIONS[reference_name]
    else:
        segments = [re.sub(r"`\d+$", "", part) for part in segments]
    return segments, substitution


def represented_type(name: str, headers_by_stem: dict[str, list[Path]],
                     documented_types: set[str]) -> dict:
    segments, substitution = normalized_segments(name)
    nested = name.rsplit(".", 1)[0] in documented_types
    # Candidate selection is based on the declaring type's header, not the
    # nested type's own file. Every candidate must contain the expected
    # namespace and a concrete owner/child declaration.
    header_stem = segments[-2] if nested else re.sub(r"`\d+$", "", name.split(".")[-1])
    expected_namespace = "::".join(segments[: -2 if nested else -1])
    for path in headers_by_stem.get(header_stem, []):
        source = stripped(path.read_text(encoding="utf-8"))
        if not re.search(r"\bnamespace\s+" + re.escape(expected_namespace) + r"\b", source):
            continue
        owner_body = named_body(source, header_stem)
        if owner_body is None:
            continue
        if nested:
            owner_declaration = next((match.group(0) for match in TYPE_PATTERN.finditer(source)
                                      if match.group(1) == header_stem), None)
            owner_kind = owner_declaration.split()[0] if owner_declaration else "class"
            if not public_nested_body(owner_body, segments[-1], owner_kind):
                continue
        return {
            "reference_name": name,
            "cpp_name": "::".join(segments),
            "classification": "host-language substitution" if substitution else "exactly represented",
            "header": str(path.relative_to(REPO)),
            "nested_public_type": nested,
        }
    return {
        "reference_name": name,
        "cpp_name": "::".join(segments),
        "classification": "missing",
        "header": None,
        "nested_public_type": nested,
    }


def reference_members(xml_dir: Path) -> tuple[list[str], list[dict]]:
    types: list[str] = []
    members: list[dict] = []
    files = [xml_dir / name for name in RUNTIME_XML_FILES]
    missing_files = [path.name for path in files if not path.is_file()]
    if missing_files:
        raise FileNotFoundError(f"missing XNA runtime XML files in {xml_dir}: {missing_files}")
    for path in files:
        for member in ET.parse(path).iter("member"):
            name = member.get("name", "")
            if len(name) < 3 or name[1] != ":":
                continue
            kind, reference_name = name[0], name[2:]
            if kind == "T":
                types.append(reference_name)
            else:
                category = {
                    "M": "constructor" if ".#ctor" in reference_name else "method",
                    "P": "property",
                    "F": "field",
                    "E": "event",
                }.get(kind)
                if category:
                    members.append({
                        "reference_name": reference_name,
                        "category": category,
                        "classification": "unreviewed",
                        "assembly_xml": path.name,
                    })
    if len(types) != len(set(types)):
        raise ValueError("duplicate documented runtime type names in reference corpus")
    return sorted(types), members


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-xml-dir", type=Path, default=DEFAULT_XML)
    parser.add_argument("--json", action="store_true", help="emit machine-readable inventory")
    parser.add_argument("--types-only", action="store_true", help="skip the full member audit")
    parser.add_argument("--write-reports", action="store_true", help="write Markdown and JSON member reports under docs/")
    parser.add_argument("--baseline", action="store_true", help="label written reports as the pre-fix baseline")
    args = parser.parse_args()

    types, members = reference_members(args.reference_xml_dir)
    headers_by_stem: dict[str, list[Path]] = {}
    for path in REPO.glob("modules/*/include/**/*.hpp"):
        headers_by_stem.setdefault(path.stem, []).append(path)
    documented_types = set(types)
    findings = [represented_type(name, headers_by_stem, documented_types) for name in types]
    missing = [entry["reference_name"] for entry in findings if entry["classification"] == "missing"]
    represented = len(types) - len(missing)
    member_counts = Counter(member["category"] for member in members)
    report = {
        "reference_xml_dir": str(args.reference_xml_dir),
        "reference_xml_sha256": {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (args.reference_xml_dir / name for name in RUNTIME_XML_FILES)
        },
        "normalization": {
            "excluded_build_time_xml": RUNTIME_EXCLUSION,
            "runtime_xml_files": RUNTIME_XML_FILES,
            "arity_substitutions": ARITY_SUBSTITUTIONS,
            "nested_types_counted_separately": True,
            "member_classification": "Microsoft XML / Microsoft DLL / CNA Clang AST audit" if not args.types_only else "not run",
        },
        "types_total": len(types),
        "types_represented": represented,
        "types_missing": missing,
        "type_surface_coverage_percent": round(100 * represented / len(types), 2),
        "type_findings": findings,
        "nested_public_types": {
            "documented": sum(entry["nested_public_type"] for entry in findings),
            "represented": sum(entry["nested_public_type"] and entry["classification"] != "missing"
                               for entry in findings),
        },
        "documented_member_counts": dict(sorted(member_counts.items())),
    }
    if not args.types_only:
        import xna_runtime_members
        member_report = xna_runtime_members.audit(args.reference_xml_dir, findings, members)
        report["member_coverage"] = member_report
        if args.write_reports:
            stem = "xna-4-runtime-member-coverage-baseline" if args.baseline else "xna-4-runtime-member-coverage"
            output = REPO / "docs"
            (output / (stem + ".json")).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            (output / (stem + ".md")).write_text(
                xna_runtime_members.markdown(member_report, len(types), represented, args.baseline),
                encoding="utf-8")
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"XNA 4.0 runtime documented public types: {len(types)}")
        print(f"CNA represented types:                    {represented}")
        print(f"Missing:                                  {len(missing)}")
        for name in missing:
            print(f"  {name}")
        print(f"Type surface coverage:                    {report['type_surface_coverage_percent']:.2f}%")
        nested_counts = report["nested_public_types"]
        print(f"Nested public types:                     {nested_counts['represented']}/{nested_counts['documented']}")
        print("Documented members (classification pending):")
        for category, count in sorted(member_counts.items()):
            print(f"  {category}: {count}")
        if not args.types_only:
            print("Member representation:")
            for category, value in member_report["categories"].items():
                print(f"  {category}: {value['represented']}/{value['documented']}")
            print(f"Strict documented-member coverage: {member_report['represented']}/{member_report['documented']} ({member_report['strict_coverage_percent']:.2f}%)")
            print(f"C++-applicable coverage: {member_report['represented']}/{member_report['cpp_applicable_denominator']} ({member_report['cpp_applicable_coverage_percent']:.2f}%)")
            for classification, count in member_report["classifications"].items():
                print(f"  {classification}: {count}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
