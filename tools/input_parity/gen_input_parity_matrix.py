#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""
INPUT-API-027 - mechanically generate the member-level parity matrix for the public
Microsoft::Xna::Framework::Input (+ ::Touch) surface.

Purpose
-------
The per-member STRICT/EXT/CNAEXT table in docs/input-public-api-frozen.md is hand-maintained and
drifts. This helper regenerates the *CNA side* directly from the public headers and pairs it with
the *FNA side* extracted from the FNA reference .cs, emitting one markdown matrix per type plus a
summary of the rows a reviewer should look at (a STRICT member with no FNA counterpart, or an FNA
public member with no CNA counterpart).

It is intentionally a *review aid*, not an authority: exact signatures are pinned by the compile
guard PublicApiInputSignatureFreezeTests.cpp (INPUT-API-031) and enum values by INPUT-API-034. The
parser is regex-based and tuned to this project's regular header style; treat flagged rows as
"look here", not "proven bug".

Usage
-----
    python3 tools/input_parity/gen_input_parity_matrix.py \
        [--fna /rv/data/library/github.com/FNA-XNA/FNA/src/Input] \
        [--out docs/input-member-parity-matrix.md]

With no --out the matrix is printed to stdout. Run from the repo root.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_HEADERS = os.path.join(REPO_ROOT, "include", "Microsoft", "Xna", "Framework", "Input")
DEFAULT_FNA = "/rv/data/library/github.com/FNA-XNA/FNA/src/Input"
DEFAULT_OUT = os.path.join(REPO_ROOT, "docs", "input-member-parity-matrix.md")

# CNA header basename (no .hpp) -> FNA .cs basename. MouseCursor has no FNA equivalent (CNAEXT class).
FNA_FILE_FOR = {
    "MouseCursor": None,  # CNAEXT: CNA-only cursor handle, no FNA source
}


# --------------------------------------------------------------------------------------------------
# C++ header extraction
# --------------------------------------------------------------------------------------------------

def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


@dataclass
class CppMember:
    name: str          # canonical identifier (getXProperty, GetState, operator==, EnumValue, ...)
    signature: str     # normalized one-line declaration
    tag: str           # STRICT | EXT | CNAEXT
    kind: str          # method | ctor | operator | field | enum


@dataclass
class CppType:
    name: str
    kind: str          # class | struct | enum
    is_cnaext_class: bool
    members: list = field(default_factory=list)


def classify_tag(decl: str, name: str, is_cnaext_class: bool) -> str:
    cnaext = is_cnaext_class or bool(re.search(r"\bCNAEXT\b", decl))
    ext = name.endswith("EXT")
    if cnaext:
        return "EXT" if ext else "CNAEXT"
    return "EXT" if ext else "STRICT"


def parse_enum(name: str, body: str) -> CppType:
    t = CppType(name=name, kind="enum", is_cnaext_class=False)
    for raw in body.split(","):
        m = re.match(r"\s*([A-Za-z_]\w*)", raw)
        if not m:
            continue
        ident = m.group(1)
        t.members.append(CppMember(
            name=ident, signature=ident,
            tag="EXT" if ident.endswith("EXT") else "STRICT", kind="enum"))
    return t


def parse_class(name: str, kind: str, body: str, header_text: str) -> CppType:
    # A whole class is CNAEXT when the CNAEXT marker sits on the class declaration itself.
    is_cnaext_class = bool(re.search(r"\bCNAEXT\b[^;{]*\b(class|struct)\b\s+" + re.escape(name),
                                    header_text))
    t = CppType(name=name, kind=kind, is_cnaext_class=is_cnaext_class)

    # Track access: struct defaults public, class defaults private.
    access = "public" if kind == "struct" else "private"
    # Walk the body statement-by-statement, honouring access-specifier labels and skipping any
    # brace-enclosed inline bodies / initializers so we only see declarations.
    i, n = 0, len(body)
    stmt = []
    depth = 0
    while i < n:
        c = body[i]
        if c in "{":
            depth += 1
            i += 1
            continue
        if c == "}":
            closed_top_level_body = depth == 1
            depth = max(0, depth - 1)
            i += 1
            if closed_top_level_body and stmt:
                # An inline method body (`Foo() const { return x_; }`, no trailing `;`) — the text
                # collected before the `{` is the whole declaration; flush it now the same way a
                # `;` would, otherwise it silently glues onto the *next* declaration's text (they
                # share no `;` between them), producing a garbled merged row in the rendered matrix.
                decl = "".join(stmt).strip()
                stmt = []
                if access == "public" and decl:
                    member = classify_cpp_decl(decl, is_cnaext_class)
                    if member:
                        t.members.append(member)
            continue
        if depth > 0:
            i += 1
            continue
        if c == ":" and body[i - 1:i].isalpha():
            # possible access label: look back at the collected token
            label = "".join(stmt).strip().split()[-1] if stmt else ""
            if label in ("public", "private", "protected"):
                access = label
                stmt = []
                i += 1
                continue
        if c == ";":
            decl = "".join(stmt).strip()
            stmt = []
            i += 1
            if access != "public" or not decl:
                continue
            member = classify_cpp_decl(decl, is_cnaext_class)
            if member:
                t.members.append(member)
            continue
        stmt.append(c)
        i += 1
    return t


def classify_cpp_decl(decl: str, is_cnaext_class: bool):
    norm = re.sub(r"\s+", " ", decl).strip()
    norm = norm.replace("[[nodiscard]] ", "")
    # operator==/!= (may be a hidden friend)
    m = re.search(r"\boperator\s*(==|!=|\[\])", norm)
    if m:
        opname = "operator" + m.group(1)
        return CppMember(name=opname, signature=norm,
                         tag=classify_tag(norm, opname, is_cnaext_class), kind="operator")
    # function / constructor: an identifier immediately followed by '('
    m = re.search(r"([A-Za-z_]\w*)\s*\(", norm)
    if m:
        fname = m.group(1)
        if fname in ("if", "for", "while", "switch", "return"):
            return None
        # crude ctor detection is not needed for the matrix; label uniformly as method/ctor
        kind = "method"
        return CppMember(name=fname, signature=norm,
                         tag=classify_tag(norm, fname, is_cnaext_class), kind=kind)
    # data member / static const: last identifier before '=' or end
    head = norm.split("=", 1)[0].strip()
    m = re.search(r"([A-Za-z_]\w*)\s*$", head)
    if m and re.search(r"\b(static|const|constexpr)\b", norm) is not None:
        fname = m.group(1)
        return CppMember(name=fname, signature=norm,
                         tag=classify_tag(norm, fname, is_cnaext_class), kind="field")
    return None


def parse_header(path: str) -> list:
    text = strip_cpp_comments(open(path, encoding="utf-8").read())
    types = []
    # enums first (so their bodies are not re-scanned as class bodies). [^\{;]+ (not just [^\{]+)
    # in the optional underlying-type clause is deliberate: a *bodyless* forward declaration like
    # `enum class FooEXT : std::uint32_t;` (used by strict-XNA headers per P1-027/P1-028 to avoid
    # #including a CNA::Input header just to declare an EXT method's return type) must NOT match
    # here. Without the `;` exclusion, the optional clause happily skips past the `;` hunting for
    # the next `{` anywhere later in the file (e.g. the following `namespace ... { class Keyboard`
    # block), misparsing that unrelated brace as the enum's own body and, as a side effect, hiding
    # the real class from the class-boundary scan below (which skips anything inside a "consumed"
    # enum span).
    ENUM_CLASS_RE = r"enum\s+class\s+([A-Za-z_]\w*)\s*(?::[^\{;]+)?\{(.*?)\}"
    for m in re.finditer(ENUM_CLASS_RE, text, flags=re.S):
        types.append(parse_enum(m.group(1), m.group(2)))
    consumed_enum_spans = [(m.start(), m.end()) for m in re.finditer(ENUM_CLASS_RE, text, flags=re.S)]

    # (?<!enum ) excludes the "class" in "enum class Foo" (a fixed-width lookbehind, matching this
    # codebase's consistent single-space "enum class" style — see ENUM_CLASS_RE's own note above for
    # why a *bodyless* enum-class forward declaration must not be misread as starting a same-named
    # ordinary class here too; [^\{;]+ carries the same `;`-stops-the-search fix as ENUM_CLASS_RE).
    for m in re.finditer(r"(?<!enum )\b(class|struct)\s+([A-Za-z_]\w*)\s*(?:final)?\s*(?::[^\{;]+)?\{", text):
        kind, name = m.group(1), m.group(2)
        if any(a <= m.start() < b for a, b in consumed_enum_spans):
            continue
        body = extract_brace_body(text, m.end() - 1)
        if body is None:
            continue
        types.append(parse_class(name, kind, body, text))
    return types


def extract_brace_body(text: str, open_brace_idx: int):
    depth = 0
    for i in range(open_brace_idx, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace_idx + 1:i]
    return None


# --------------------------------------------------------------------------------------------------
# C# (FNA) extraction
# --------------------------------------------------------------------------------------------------

@dataclass
class CsMember:
    name: str
    kind: str          # property | method | operator | field | enum
    visibility: str    # public | internal | protected | private


def strip_cs_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def parse_cs(path: str) -> dict:
    text = strip_cs_comments(open(path, encoding="utf-8").read())
    out = {}

    for m in re.finditer(r"\benum\s+([A-Za-z_]\w*)\s*(?::[^\{]+)?\{(.*?)\}", text, flags=re.S):
        vals = []
        for raw in m.group(2).split(","):
            mm = re.match(r"\s*([A-Za-z_]\w*)", raw)
            if mm:
                vals.append(CsMember(mm.group(1), "enum", "public"))
        out[m.group(1)] = vals

    for m in re.finditer(r"\b(?:public|internal)\s+(?:sealed\s+|static\s+|partial\s+)*"
                         r"(class|struct)\s+([A-Za-z_]\w*)", text):
        tname = m.group(2)
        if tname in out:
            continue
        out[tname] = extract_cs_members(text, tname)
    return out


def extract_cs_members(text: str, type_name: str) -> list:
    members = []
    seen = set()
    vis_re = r"(public|internal|protected|private)"
    # operators
    for m in re.finditer(vis_re + r"\s+static\s+[\w<>\[\],\.\s]+?\boperator\s*(==|!=|\[\])\s*\(", text):
        key = ("op", m.group(2))
        if key not in seen:
            seen.add(key)
            members.append(CsMember("operator" + m.group(2), "operator", m.group(1)))
    # indexer: `<vis> TYPE this[...]`
    for m in re.finditer(vis_re + r"\s+[\w<>\[\],\.\?]+\s+this\s*\[", text):
        key = ("idx", "this[]")
        if key not in seen:
            seen.add(key)
            members.append(CsMember("this[]", "indexer", m.group(1)))
    # constructor: `<vis> TypeName(` — no return type between visibility and the type name.
    for m in re.finditer(vis_re + r"\s+" + re.escape(type_name) + r"\s*\(", text):
        key = ("ctor", type_name)
        if key not in seen:
            seen.add(key)
            members.append(CsMember(type_name, "ctor", m.group(1)))
    # properties: `<vis> [static] TYPE Name {` (get/set)
    for m in re.finditer(vis_re + r"\s+(?:static\s+|override\s+|readonly\s+)*"
                         r"[\w<>\[\],\.\?]+\s+([A-Za-z_]\w*)\s*\{", text):
        name = m.group(2)
        if name == type_name:
            continue
        key = ("prop", name)
        if key not in seen:
            seen.add(key)
            members.append(CsMember(name, "property", m.group(1)))
    # methods: `<vis> [static/override] TYPE Name(`
    for m in re.finditer(vis_re + r"\s+(?:static\s+|override\s+|virtual\s+|sealed\s+)*"
                         r"[\w<>\[\],\.\?]+\s+([A-Za-z_]\w*)\s*\(", text):
        name = m.group(2)
        if name in ("if", "for", "while", "switch", "operator"):
            continue
        key = ("meth", name)
        if key not in seen:
            seen.add(key)
            members.append(CsMember(name, "method", m.group(1)))
    # const / static readonly fields: `<vis> [static] (const|readonly) TYPE Name`
    for m in re.finditer(vis_re + r"\s+(?:static\s+)?(?:const|readonly)\s+"
                         r"[\w<>\[\],\.\?]+\s+([A-Za-z_]\w*)\s*[=;]", text):
        name = m.group(2)
        key = ("field", name)
        if key not in seen:
            seen.add(key)
            members.append(CsMember(name, "field", m.group(1)))
    return members


# --------------------------------------------------------------------------------------------------
# Cross-check
# --------------------------------------------------------------------------------------------------

# C# IList<T>/IEnumerator/IEnumerable/IDisposable plumbing that CNA's value-type collections do NOT
# mirror by design (CNA exposes operator[]/getCountProperty instead). Documented as an intentional
# deviation, not a parity gap.
CS_INTERFACE_PLUMBING = {
    "getenumerator", "movenext", "current", "dispose", "reset", "add", "remove", "removeat",
    "insert", "clear", "contains", "copyto", "indexof",
}


def cna_canonical_keys(m: CppMember) -> set:
    """The FNA-side identifier(s) a CNA member should correspond to."""
    if m.name in ("operator[]", "getItem"):
        return {"this[]"}
    pm = re.match(r"(?:get|set)([A-Z]\w*)Property$", m.name)
    if pm:
        return {pm.group(1).lower()}
    return {m.name.lower()}


def cs_canonical_key(m: CsMember) -> str:
    return m.name.lower()


def cross_check(cna_types: dict, fna: dict):
    """Return (per_type_rows, gap_strict, gap_fna, gap_interface)."""
    per_type = {}
    gap_strict = []      # CNA STRICT/EXT members with no FNA counterpart
    gap_fna = []         # FNA public members with no CNA counterpart
    gap_interface = []   # FNA collection/enumerator plumbing CNA omits by design

    for tname, ctype in cna_types.items():
        fna_name = tname if tname not in FNA_FILE_FOR else FNA_FILE_FOR[tname]
        fna_members = fna.get(fna_name, []) if fna_name else []
        fna_public = [x for x in fna_members if x.visibility == "public"]
        fna_keys = {cs_canonical_key(x) for x in fna_public}
        # 'internal' FNA members are surfaced by CNA as CNAEXT; keep them for the CNAEXT cross-check.
        fna_all_keys = {cs_canonical_key(x) for x in fna_members}

        rows = []
        for cm in ctype.members:
            keys = cna_canonical_keys(cm)
            in_fna_public = bool(keys & fna_keys)
            in_fna_any = bool(keys & fna_all_keys)
            note = ""
            is_special = bool(re.search(r"=\s*(delete|default)\b", cm.signature))
            if cm.tag in ("STRICT", "EXT"):
                if in_fna_public:
                    note = "matched"
                elif in_fna_any:
                    note = "FNA-internal (review tag)"
                elif fna_name is None:
                    note = "no FNA source (type is CNA-only)"
                elif is_special:
                    # `= delete` / `= default` special members are a C++ idiom (e.g. making a static
                    # class non-instantiable, or the implicit-equivalent value-struct default ctor).
                    # C# has no such declaration, so absence from FNA is expected, not a gap.
                    note = "C++ special-member idiom (no XNA counterpart expected)"
                elif cm.tag == "EXT":
                    # EXT = FNA-compatible extension; by definition has no stock-XNA public member.
                    note = "EXT extension (no stock-XNA counterpart expected)"
                else:
                    note = "NO FNA COUNTERPART"
                    gap_strict.append((tname, cm))
            else:  # CNAEXT
                note = "CNA-only" if not in_fna_any else "maps FNA non-public"
            rows.append((cm, "yes" if in_fna_public else ("internal" if in_fna_any else "no"), note))

        # FNA public members with no CNA counterpart
        cna_keys = set()
        for cm in ctype.members:
            cna_keys |= cna_canonical_keys(cm)
        missing = []
        for fm in fna_public:
            if cs_canonical_key(fm) not in cna_keys:
                if cs_canonical_key(fm) in CS_INTERFACE_PLUMBING:
                    gap_interface.append((tname, fm))
                    continue
                # Equals/GetHashCode/ToString map to CNA same-named methods; already covered.
                missing.append(fm)
                gap_fna.append((tname, fm))
        per_type[tname] = (ctype, rows, missing, fna_name)
    return per_type, gap_strict, gap_fna, gap_interface


# --------------------------------------------------------------------------------------------------
# Rendering
# --------------------------------------------------------------------------------------------------

def render(per_type: dict, gap_strict: list, gap_fna: list, gap_interface: list, fna_root: str) -> str:
    out = []
    out.append("# Input member-level parity matrix (INPUT-API-027)")
    out.append("")
    out.append("> **Related input docs (INP-0003):** [plan](../plans/plan_input.md) · "
               "[backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · "
               "[member-parity matrix](input-member-parity-matrix.md) · "
               "[frozen API + tier glossary](input-public-api-frozen.md) · "
               "[test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · "
               "[platform notes](platform-input-notes.md) · "
               "[manual results](input-manual-verification-results.md) · "
               "[demo checklist](demo-input-checklist.md)")
    out.append("")
    out.append("> **Generated** by `tools/input_parity/gen_input_parity_matrix.py` from the public")
    out.append("> `Microsoft::Xna::Framework::Input` (+ `::Touch`) headers and the FNA reference")
    out.append(f"> at `{fna_root}`. Do not hand-edit — re-run the generator. This is a review aid;")
    out.append("> exact signatures stay pinned by `PublicApiInputSignatureFreezeTests.cpp`")
    out.append("> (INPUT-API-031) and enum values by INPUT-API-034.")
    out.append("")
    out.append("Tags: **STRICT** = XNA 4.0 API (must match FNA) · **EXT** = FNA-compatible extension "
               "(`…EXT`) · **CNAEXT** = CNA-only convenience.")
    out.append("")
    out.append("The `In FNA` column: `yes` = a `public` FNA member matches by name; `internal` = matches "
               "an FNA `internal` member (expected for a CNA `CNAEXT` that surfaces FNA-internal plumbing); "
               "`no` = no FNA member of that name (expected for `= delete`/`= default` C++ idioms and EXT).")
    out.append("")

    # Summary of review rows first.
    out.append("## Review summary")
    out.append("")
    if not gap_strict and not gap_fna:
        out.append("No STRICT/EXT member is missing an FNA counterpart, and no FNA public member is")
        out.append("missing a CNA counterpart (by the heuristic name mapping, after accounting for the")
        out.append("documented collection-interface deviations below). Full per-type tables follow.")
    else:
        out.append(f"- STRICT/EXT CNA members with **no FNA counterpart**: **{len(gap_strict)}**")
        for tname, cm in gap_strict:
            out.append(f"  - `{tname}::{cm.name}` ({cm.tag}) — `{cm.signature}`")
        out.append(f"- FNA public members with **no CNA counterpart**: **{len(gap_fna)}**")
        for tname, fm in gap_fna:
            out.append(f"  - `{tname}.{fm.name}` ({fm.kind})")
    out.append("")
    if gap_interface:
        out.append(f"- FNA `IList<T>`/`IEnumerator`/`IDisposable` plumbing intentionally **not** mirrored "
                   f"by CNA's value-type collections (by design, not a gap): **{len(gap_interface)}** — "
                   + ", ".join(f"`{t}.{m.name}`" for t, m in gap_interface))
        out.append("")
    out.append("> Rows flagged above are heuristic (name-level) and may be false positives — e.g. a")
    out.append("> C++ `ref`/`&&` overload pair mapping one C# member, an FNA `internal` surfaced as CNA")
    out.append("> CNAEXT, or an equality operator resolved via ADL. Review each against the .cs before acting.")
    out.append("")

    for tname in sorted(per_type):
        ctype, rows, missing, fna_name = per_type[tname]
        src = f"FNA `{fna_name}.cs`" if fna_name else "no FNA source (CNA-only)"
        out.append(f"## `{tname}` — {ctype.kind} ({src})")
        out.append("")
        if not rows:
            out.append("_No public members extracted._")
            out.append("")
            continue
        out.append("| Member | Tag | In FNA | Note |")
        out.append("|--------|-----|--------|------|")
        for cm, in_fna, note in rows:
            sig = cm.signature.replace("|", "\\|")
            out.append(f"| `{sig}` | {cm.tag} | {in_fna} | {note} |")
        out.append("")
        if missing:
            out.append(f"**FNA public members with no CNA counterpart:** "
                       + ", ".join(f"`{m.name}` ({m.kind})" for m in missing))
            out.append("")
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------------------------------

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--headers", default=DEFAULT_HEADERS)
    ap.add_argument("--fna", default=DEFAULT_FNA)
    ap.add_argument("--out", default=None, help="output path (default: stdout)")
    args = ap.parse_args(argv)

    if not os.path.isdir(args.headers):
        print(f"error: headers dir not found: {args.headers}", file=sys.stderr)
        return 2

    # Collect headers (top-level + Touch/).
    header_paths = []
    for root, _dirs, files in os.walk(args.headers):
        for f in sorted(files):
            if f.endswith(".hpp"):
                header_paths.append(os.path.join(root, f))
    header_paths.sort()

    cna_types = {}
    for p in header_paths:
        for t in parse_header(p):
            cna_types[t.name] = t

    fna = {}
    if os.path.isdir(args.fna):
        for root, _dirs, files in os.walk(args.fna):
            for f in files:
                if f.endswith(".cs"):
                    fna.update(parse_cs(os.path.join(root, f)))
    else:
        print(f"warning: FNA dir not found ({args.fna}); FNA columns will be empty.",
              file=sys.stderr)

    per_type, gap_strict, gap_fna, gap_interface = cross_check(cna_types, fna)
    md = render(per_type, gap_strict, gap_fna, gap_interface, args.fna)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(md)
        print(f"wrote {args.out}: {len(cna_types)} types, "
              f"{len(gap_strict)} STRICT/EXT gaps, {len(gap_fna)} FNA-only members",
              file=sys.stderr)
    else:
        sys.stdout.write(md)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
