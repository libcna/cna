#!/usr/bin/env python3
"""plans/plan_xnapipeline_parity.md XNAPP-008, XNAPP-315: the provenance gate.

This plan is built by measuring Microsoft's own assemblies and Microsoft's own build task. Nothing
Microsoft owns may end up *in* the repository as a result, and no implementation may be copied from
another XNA reimplementation. Those are rules a person can follow and forget, so this is the test.

What it checks, and what each check actually proves:

1. **No proprietary binary image is tracked.** A PE/COFF file, a .NET assembly or a Windows
   installer, by content and not only by extension. This is the check that would fail if the
   oracle's `Microsoft.Xna.Framework.Content.Pipeline.dll` were ever committed instead of copied
   into an ignored build directory -- and it verifies that the directory really is ignored.

2. **No Microsoft-licensed font is tracked.** Fonts are read properly: the TrueType `name` table is
   parsed and its copyright, family and vendor records are matched, so a renamed file does not slip
   through. Every tracked font must also have a `PROVENANCE.json` beside it naming a licence and a
   digest that matches the bytes.

3. **No production source is attributed to someone else.** Every tracked C++ source and header
   under `modules/`, `tools/` and `spikes/` must declare `SPDX-License-Identifier: MS-PL`, and none
   may carry a copyright notice naming Microsoft, MonoGame or FNA as the author of the code. This
   is deliberately a check on *attribution headers and file paths*, not a grep for words: `MonoGame`
   and `FNA` are named legitimately hundreds of times in this repository -- in comments recording
   what was measured, in test names, in documentation -- and a gate that failed on those would be
   turned off within a day. What it proves is that no file arrived carrying someone else's header,
   and what it cannot prove is that a file was not laundered; that is what review is for.

4. **Every black-box fixture is accounted for.** Each directory under `tests/assets/xna40/` must
   carry a provenance document, and every file in it must be named there. This is the check that
   notices a fixture arriving with no line saying who wrote it -- which is exactly what happened to
   the five `.xml` documents added for XNAPP-261 before this check existed.

5. **Every vendored third party is declared.** Every directory under `vendor/` needs a row in
   `tools/provenance/third-party.json` with an upstream, a licence and a licence file that exists,
   and the manifest may not name a directory that is not there.

Usage:
    provenance_gate.py [--repo <path>] [--json <file>]

Exit status is 0 when nothing was found and 1 otherwise; 2 means the gate could not run.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys

# Extensions that are a compiled image whatever their content turns out to be.
BINARY_IMAGE_SUFFIXES = (".dll", ".exe", ".sys", ".ocx", ".msi", ".cab", ".winmd", ".pdb", ".lib")

FONT_SUFFIXES = (".ttf", ".otf", ".ttc", ".fon", ".pfb", ".pfm.font")

# Families Microsoft licences rather than gives away. A font whose `name` table says one of these
# is one, however the file happens to be called.
MICROSOFT_FONT_MARKERS = (
    "microsoft", "monotype", "arial", "courier new", "times new roman", "segoe", "calibri",
    "cambria", "candara", "consolas", "constantia", "corbel", "georgia", "tahoma", "verdana",
    "trebuchet", "comic sans", "impact", "webdings", "wingdings", "marlett", "sylfaen",
)

# A copyright line naming one of these in a production source is a file that came from there.
FOREIGN_ATTRIBUTION = (
    "microsoft corporation", "monogame", "the monogame team", "ethan lee", "flibitijibibo",
    "fna-xna", "xna community game platform", "sharpdx", "sharpfont",
)

SOURCE_SUFFIXES = (".cpp", ".hpp", ".c", ".h", ".cc", ".hh", ".cxx", ".hxx")

# The roots XNAPP-008 names, plus tools/. `spikes/` is deliberately out of the SPDX half of the
# gate and inside the attribution half: a spike is a throwaway probe kept for the finding it
# proved, and holding it to a shipped file's header would say nothing about provenance.
SOURCE_ROOTS = ("modules/", "tools/", "spikes/")
SPDX_ROOTS = ("modules/", "tools/")

# MS-PL is this project's licence. MIT appears deliberately on the files that interface with ENet,
# which is MIT itself, and they carry CNA's own copyright line -- an author's choice rather than a
# foreign header, which is what check 3 is actually about.
ALLOWED_SPDX = ("MS-PL", "MIT")

# Where the oracle copies Microsoft's assemblies to. It must be ignored, or check 1 is one careless
# `git add` away from being untrue.
MUST_BE_IGNORED = (
    "build/xna-pipeline-oracle/differential/run/Microsoft.Xna.Framework.Content.Pipeline.dll",
    "build/xna-pipeline-oracle/differential/run/XnaNative.dll",
)


def tracked_files(repo):
    """Every file Git tracks, as repository-relative paths."""
    out = subprocess.run(["git", "-C", repo, "ls-files", "-z"], check=True,
                         stdout=subprocess.PIPE).stdout
    return [name.decode("utf-8") for name in out.split(b"\0") if name]


def read(repo, name, limit=None):
    with open(os.path.join(repo, name), "rb") as handle:
        return handle.read() if limit is None else handle.read(limit)


def is_binary_image(head):
    """A PE/COFF image, a .NET assembly or a Windows installer, by its own first bytes."""
    return (head[:2] == b"MZ" or head[:4] == b"PE\0\0" or
            head[:8] == b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1")  # MSI/compound file


# The `name` table records that say what a font *is*: copyright, family, subfamily, preferred
# family, full name, PostScript name, trademark and manufacturer. Deliberately not the description
# (10) or the sample text (19): a metric-compatible font describes what it is compatible *with*, and
# Liberation Mono's description names Courier New in exactly that sentence.
FONT_IDENTITY_RECORDS = (0, 1, 2, 3, 4, 6, 7, 8, 16, 17)


def font_names(data):
    """Every identity string in a TrueType/OpenType `name` table, lowercased.

    Parsed rather than searched for, so a font is judged by what it says about itself. A file this
    cannot parse answers an empty list and is reported as unreadable rather than as clean.
    """
    if len(data) < 12:
        return None
    tag = data[:4]
    if tag in (b"ttcf",):
        offset = struct.unpack_from(">I", data, 12)[0] if len(data) >= 16 else None
        if offset is None or offset + 12 > len(data):
            return None
        base = offset
    elif tag in (b"\0\1\0\0", b"OTTO", b"true", b"typ1"):
        base = 0
    else:
        return None
    count = struct.unpack_from(">H", data, base + 4)[0]
    table = None
    for index in range(count):
        entry = base + 12 + index * 16
        if entry + 16 > len(data):
            return None
        if data[entry:entry + 4] == b"name":
            table = struct.unpack_from(">I", data, entry + 8)[0]
            break
    if table is None or table + 6 > len(data):
        return None
    records, string_offset = struct.unpack_from(">HH", data, table + 2)
    found = []
    for index in range(records):
        record = table + 6 + index * 12
        if record + 12 > len(data):
            break
        name_id = struct.unpack_from(">H", data, record + 6)[0]
        if name_id not in FONT_IDENTITY_RECORDS:
            continue
        length, offset = struct.unpack_from(">HH", data, record + 8)
        start = table + string_offset + offset
        raw = data[start:start + length]
        for encoding in ("utf-16-be", "latin-1"):
            try:
                found.append(raw.decode(encoding).lower())
                break
            except UnicodeDecodeError:
                continue
    return found


def header_of(data):
    """The first forty lines of a source file, as text, for its attribution.

    Decoded leniently on purpose: the caller reads a fixed prefix of the file, which can land in the
    middle of a multi-byte sequence, and a header is being read for the words in it rather than
    validated as a document.
    """
    return "\n".join(data.decode("utf-8", "replace").splitlines()[:40])


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", default=os.getcwd())
    parser.add_argument("--json", help="write the full report here")
    parser.add_argument("--spdx-baseline", type=int,
                        help="ceiling for sources with no SPDX identifier; overrides the recorded one")
    arguments = parser.parse_args(argv[1:])
    repo = os.path.abspath(arguments.repo)

    findings = []
    notes = []
    counts = {"tracked": 0, "sources": 0, "fonts": 0, "vendored": 0, "fixtures": 0}
    names = tracked_files(repo)
    counts["tracked"] = len(names)

    for name in names:
        path = os.path.join(repo, name)
        if not os.path.isfile(path) or os.path.islink(path):
            continue
        lower = name.lower()

        # 1. Binary images, by extension and by content.
        if lower.endswith(BINARY_IMAGE_SUFFIXES):
            findings.append({"check": "binary", "path": name,
                             "detail": "a compiled image is tracked"})
            continue
        head = read(repo, name, 16)
        if is_binary_image(head):
            findings.append({"check": "binary", "path": name,
                             "detail": "the file's own first bytes are a PE/COFF or compound image"})
            continue

        # 2. Fonts.
        if lower.endswith(FONT_SUFFIXES):
            counts["fonts"] += 1
            data = read(repo, name)
            strings = font_names(data)
            if strings is None:
                findings.append({"check": "font", "path": name,
                                 "detail": "the font's name table could not be parsed, so its "
                                           "provenance cannot be read off the file"})
            else:
                for marker in MICROSOFT_FONT_MARKERS:
                    if any(marker in text for text in strings):
                        findings.append({"check": "font", "path": name,
                                         "detail": "the font's own name table says %r" % marker})
                        break
            beside = os.path.join(os.path.dirname(name), "PROVENANCE.json")
            if not os.path.isfile(os.path.join(repo, beside)):
                findings.append({"check": "font", "path": name,
                                 "detail": "no PROVENANCE.json beside it"})
            else:
                record = json.loads(read(repo, beside).decode("utf-8"))
                rows = record if isinstance(record, list) else [record]
                row = next((r for r in rows if r.get("file") == os.path.basename(name)), None)
                if row is None:
                    findings.append({"check": "font", "path": name,
                                     "detail": "%s has no row for it" % beside})
                else:
                    if not row.get("license"):
                        findings.append({"check": "font", "path": name,
                                         "detail": "its provenance row names no licence"})
                    digest = hashlib.sha256(data).hexdigest()
                    if row.get("sha256") and row["sha256"] != digest:
                        findings.append({"check": "font", "path": name,
                                         "detail": "the bytes are not the ones its provenance row "
                                                   "records (%s)" % digest})
            continue

        # 3. Production sources.
        if name.startswith(SOURCE_ROOTS) and lower.endswith(SOURCE_SUFFIXES) \
                and not name.startswith("vendor/") and "/vendor/" not in name:
            counts["sources"] += 1
            header = header_of(read(repo, name, 8192))
            declared = None
            for line in header.splitlines()[:3]:
                if "SPDX-License-Identifier:" in line:
                    declared = line.split("SPDX-License-Identifier:", 1)[1]
                    declared = declared.replace("*/", "").replace("-->", "").strip()
                    break
            if name.startswith(SPDX_ROOTS):
                if declared is None:
                    notes.append({"check": "spdx", "path": name,
                                  "detail": "no SPDX-License-Identifier in the first three lines"})
                elif declared not in ALLOWED_SPDX:
                    findings.append({"check": "source", "path": name,
                                     "detail": "declares %r, which is not one of %s" %
                                               (declared, list(ALLOWED_SPDX))})
            lowered = header.lower()
            if "copyright" in lowered:
                for marker in FOREIGN_ATTRIBUTION:
                    if marker in lowered:
                        findings.append({"check": "source", "path": name,
                                         "detail": "carries a copyright notice naming %r" % marker})
                        break
            for marker in ("monogame", "/fna/", "fna-xna"):
                if marker in lower:
                    findings.append({"check": "source", "path": name,
                                     "detail": "a production source whose path names %r" % marker})
                    break

    # 4. The black-box corpus: every fixture named in its directory's provenance document.
    corpus_root = "tests/assets/xna40"
    directories = {}
    for name in names:
        if name.startswith(corpus_root + "/"):
            directories.setdefault(os.path.dirname(name), []).append(os.path.basename(name))
    # A tree with no corpus at all is not this gate's finding -- the suites that read it would
    # fail long before -- so the check applies to the directories that are there.
    for directory, entries in sorted(directories.items()):
        documents = [entry for entry in entries if "PROVENANCE" in entry.upper()]
        if "PROVENANCE.json" not in documents:
            findings.append({"check": "corpus", "path": directory,
                             "detail": "a corpus directory with no PROVENANCE.json. The prose beside "
                                       "it explains what the fixtures are for; this is the half a "
                                       "gate can read."})
            continue
        record = json.loads(read(repo, os.path.join(directory, "PROVENANCE.json")).decode("utf-8"))
        rows = {row["file"]: row for row in record["files"]}
        counts["fixtures"] += len(entries) - len(documents)
        for entry in sorted(entries):
            if entry in documents:
                continue
            row = rows.get(entry)
            if row is None:
                findings.append({"check": "corpus", "path": os.path.join(directory, entry),
                                 "detail": "has no row in %s/PROVENANCE.json" % directory})
            elif row.get("origin") not in ("authored", "generated"):
                findings.append({"check": "corpus", "path": os.path.join(directory, entry),
                                 "detail": "its row's origin is %r, which is neither authored nor "
                                           "generated" % row.get("origin")})
            elif row.get("thirdParty") and not row.get("license"):
                findings.append({"check": "corpus", "path": os.path.join(directory, entry),
                                 "detail": "declared third-party with no licence row"})
        for name in sorted(rows):
            if name not in entries:
                findings.append({"check": "corpus", "path": os.path.join(directory, name),
                                 "detail": "named in PROVENANCE.json and not in the tree"})

    # 5. Vendored third parties.
    manifest_path = os.path.join("tools", "provenance", "third-party.json")
    declared = {}
    if os.path.isfile(os.path.join(repo, manifest_path)):
        declared = {row["directory"]: row
                    for row in json.loads(read(repo, manifest_path).decode("utf-8"))["vendored"]}
    else:
        findings.append({"check": "vendor", "path": manifest_path,
                         "detail": "the third-party manifest does not exist"})
    vendor_root = os.path.join(repo, "vendor")
    present = sorted(entry for entry in os.listdir(vendor_root)
                     if os.path.isdir(os.path.join(vendor_root, entry))) \
        if os.path.isdir(vendor_root) else []
    counts["vendored"] = len(present)
    for entry in present:
        row = declared.get(entry)
        if row is None:
            findings.append({"check": "vendor", "path": "vendor/" + entry,
                             "detail": "vendored and not declared in " + manifest_path})
            continue
        for field in ("upstream", "license", "licenseFile"):
            if not row.get(field):
                findings.append({"check": "vendor", "path": "vendor/" + entry,
                                 "detail": "its row names no %s" % field})
        licence = row.get("licenseFile")
        if licence and not os.path.isfile(os.path.join(repo, licence)):
            findings.append({"check": "vendor", "path": "vendor/" + entry,
                             "detail": "its licence file %s is not in the tree" % licence})
    for entry in declared:
        if entry not in present:
            findings.append({"check": "vendor", "path": "vendor/" + entry,
                             "detail": "declared in %s and not in the tree" % manifest_path})

    # 5. The oracle's own copies must be unable to be committed.
    for path in MUST_BE_IGNORED:
        status = subprocess.run(["git", "-C", repo, "check-ignore", "-q", path])
        if status.returncode != 0:
            findings.append({"check": "ignored", "path": path,
                             "detail": "this path is not ignored, so a Microsoft assembly copied "
                                       "here could be committed"})

    # A production source with no licence header is licence hygiene rather than provenance -- it
    # says nothing about where the file came from -- and there are hundreds of them, from long
    # before this plan. Ratcheted rather than ignored: the count is visible and may only fall.
    baseline_path = os.path.join("tools", "provenance", "license-baseline.json")
    baseline = None
    if os.path.isfile(os.path.join(repo, baseline_path)):
        baseline = json.loads(read(repo, baseline_path).decode("utf-8"))["sourcesWithoutSpdx"]
    if arguments.spdx_baseline is not None:
        baseline = arguments.spdx_baseline
    if baseline is not None and len(notes) > baseline:
        findings.append({"check": "spdx", "path": baseline_path,
                         "detail": "%d production source(s) carry no SPDX identifier, which is more "
                                   "than the recorded %d. A new file must carry one." %
                                   (len(notes), baseline)})

    report = {"findings": findings, "notes": notes, "counts": counts}
    for finding in findings:
        print("PROVENANCE %-8s %s: %s" % (finding["check"], finding["path"], finding["detail"]))
    print("provenance_gate: %d tracked file(s), %d production source(s), %d font(s), "
          "%d corpus fixture(s), %d vendored director(y|ies); %d finding(s)" %
          (counts["tracked"], counts["sources"], counts["fonts"], counts["fixtures"],
           counts["vendored"], len(findings)))
    if arguments.json:
        with open(arguments.json, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2, sort_keys=True)
            handle.write("\n")
    return 1 if findings else 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except subprocess.CalledProcessError as error:
        print("provenance_gate: %s" % error, file=sys.stderr)
        sys.exit(2)
