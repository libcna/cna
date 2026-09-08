#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-001/002: freeze the genuine reference corpus.

Walks the read-only sample artefact root, decides which `.xnb` files were produced by the genuine
Microsoft XNA 4.0 Content Pipeline rather than by CNA or FNA, and records for each one everything a
differential sweep needs to know before it has a source asset: its path, size, SHA-256, container
header, compression, root reader and complete type-reader table.

The decision is made from the artefact root's own directory conventions, which every sample in it
follows and whose `MANIFEST.md` files document: `xna4-build/`, `xna4-diagnostic/`, `win7-export/`,
`win7-build/`, `xnb-staging/` and `SAMPLES-DEC-007-Win7-SongProcessor/export/` hold output of the
real pipeline; `xna4-original/` holds `.xnb` Microsoft shipped inside the sample itself; anything
under a `cna-*`, `fna-*`, `mgcb` or `evidence` directory is somebody else's output and is not a
reference.

Nothing here writes to the read-only root.

Usage:
    corpus_inventory.py [--root <dir>] [--out <manifest.json>] [--workers N]
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import posixpath
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "xnb"))
import xnb_conformance  # noqa: E402

DEFAULT_ROOT = "/rv/tmp/samples"

# Directory names that mark output of the genuine Microsoft pipeline, and what each one is.
GENUINE_DIRS = {
    "xna4-build": "xna4-build",
    "xna4-diagnostic": "xna4-diagnostic",
    "xna4-diagnostic-noaudio": "xna4-diagnostic",
    "xna4-diag": "xna4-diagnostic",
    "xna4-migrated": "xna4-migrated",
    "xna4-local-normalized-snapshot": "xna4-normalized",
    "win7-export": "win7-export",
    "win7-build": "win7-build",
    "win7-work": "win7-work",
    "xnb-staging": "xnb-staging",
    "xna4-original": "shipped-with-sample",
    "xna3-original": "shipped-with-sample-xna3",
    "xna2-original": "shipped-with-sample-xna2",
    "xna-original": "shipped-with-sample",
    "original": "shipped-with-sample",
    "xbox-refs": "xbox-refs",
    # SAMPLES-DEC-007's `export/` holds the Windows 7 virtual machine's own `SongProcessor` run:
    # seven `.xnb`/`.wma` pairs plus its build scripts and logs, and its README is the record.
    # No other sample has a directory of this name.
    "export": "win7-songprocessor",
}

# Files that sit in a genuine directory but were not written by the genuine pipeline. Each names
# the evidence that says so. `SAMPLE-013`'s song is written by `scripts/BuildSongXnb.cs`, a
# hand-written `BinaryWriter` in the sample's own build script: it emits one type reader where
# XNA's `SongProcessor` emits two (`SongReader` and the `Int32Reader` its duration goes through,
# which all eight genuine songs in this corpus carry), so comparing against it would mark CNA's
# correct output wrong.
SYNTHESIZED = {
    "SAMPLE-013-Platformer_4_0/xna4-build/Content/Sounds/Music.xnb":
        "written by SAMPLE-013-Platformer_4_0/scripts/BuildSongXnb.cs, not by BuildContent",
    "SAMPLE-013-Platformer_4_0/xna4-build/bin/Content/Sounds/Music.xnb":
        "copy of the file SAMPLE-013-Platformer_4_0/scripts/BuildSongXnb.cs wrote",
}

# MonoGame's content builder leaves a `PipelineBuildEvent` document (`.mgcontent`) beside every
# `.xnb` it writes, naming the `.xnb` in `<DestFile>`. That is the evidence a directory holding
# MonoGame output is a MonoGame output directory whatever it is called: `SAMPLE-003`'s
# `xna4-original/Content-built/` is one, and its name puts it under a genuine root
# (`plans/plan_xna_sample_xnb_sweep.md` `XNASWEEP-132`).
MGCONTENT_DEST = re.compile(rb"<DestFile>([^<]*)</DestFile>")


def monogame_destinations(root: str):
    """Corpus-relative directories a MonoGame `.mgcontent` names as a build destination."""
    destinations = set()
    for directory, subdirs, files in os.walk(root):
        subdirs.sort()
        for name in files:
            if not name.lower().endswith(".mgcontent"):
                continue
            with open(os.path.join(directory, name), "rb") as handle:
                document = handle.read()
            for match in MGCONTENT_DEST.finditer(document):
                written = match.group(1).decode("utf-8", "replace").replace("\\", "/")
                if not written:
                    continue
                absolute = written if os.path.isabs(written) else os.path.join(directory, written)
                relative = os.path.relpath(os.path.dirname(absolute), root)
                if not relative.startswith(".."):
                    destinations.add(relative.replace(os.sep, "/"))
    return destinations


# Directory names that mark output of something that is not the genuine pipeline. Checked first.
FOREIGN_PREFIXES = ("cna-", "fna-", "mgcb")
FOREIGN_EXACT = {"cna-build", "cna-content", "cna-diag", "cna-diagnostic", "evidence",
                 "fixtures", "expanded", "out", "diagnostic", "diagnostics",
                 "cna-source-2d", "cna-source-diagnostic", "modern-fna-original"}


def classify(relative: str, monogame=()):
    """Answers (provenance, genuine) for a corpus-relative `.xnb` path."""
    posix = relative.replace(os.sep, "/")
    if posix in SYNTHESIZED:
        return "synthesized", False
    if posixpath.dirname(posix) in monogame:
        return "monogame", False
    parts = relative.split(os.sep)
    for part in parts[:-1]:
        if part in FOREIGN_EXACT or part.startswith(FOREIGN_PREFIXES):
            return part, False
    for part in parts[:-1]:
        if part in GENUINE_DIRS:
            return GENUINE_DIRS[part], True
    return parts[1] if len(parts) > 1 else "?", False


def header_of(path: str):
    """The container header, read without trusting the payload to parse."""
    with open(path, "rb") as handle:
        head = handle.read(14)
    if len(head) < 10 or head[0:3] != b"XNB":
        return None
    flags = head[5]
    return {
        "platform": chr(head[3]),
        "version": head[4],
        "flags": flags,
        "hidef": bool(flags & 0x01),
        "compressedLzx": bool(flags & 0x80),
        "compressedLz4": bool(flags & 0x40),
        "declaredSize": int.from_bytes(head[6:10], "little"),
        "decompressedSize": int.from_bytes(head[10:14], "little") if flags & 0xC0 else None,
    }


def one(job):
    root, relative = job
    path = os.path.join(root, relative)
    entry = {"relative": relative.replace(os.sep, "/")}
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError as error:
        entry["status"] = "unreadable"
        entry["error"] = str(error)
        return entry
    entry["size"] = len(data)
    entry["sha256"] = hashlib.sha256(data).hexdigest()
    head = header_of(path)
    if head is None:
        entry["status"] = "not-xnb"
        return entry
    entry.update(head)
    try:
        report = xnb_conformance.parse(path)
        entry["status"] = "parsed"
        entry["rootReader"] = report.get("rootReader")
        entry["typeReaders"] = report.get("typeReaders")
        entry["typeReaderNames"] = report.get("typeReaderNames")
        entry["sharedResourceCount"] = report.get("sharedResourceCount")
        entry["graphicsProfile"] = report.get("graphicsProfile")
        entry["compression"] = report.get("compression")
        entry["platformName"] = report.get("platformName")
        root_value = report.get("root")
        if isinstance(root_value, dict) and "kind" in root_value:
            entry["rootKind"] = root_value["kind"]
    except xnb_conformance.XnbError as error:
        entry["status"] = "unparsed"
        entry["error"] = str(error)
        # The reader table is still worth having: it is written before anything the payload
        # parser can trip on, so read it directly.
        try:
            entry.update(reader_table_only(data, head))
        except Exception as inner:  # noqa: BLE001 - a best effort, recorded either way
            entry["tableError"] = str(inner)
    except Exception as error:  # noqa: BLE001 - a corpus entry never stops the sweep
        entry["status"] = "crashed"
        entry["error"] = "%s: %s" % (type(error).__name__, error)
    return entry


def reader_table_only(data: bytes, head):
    """The type-reader table of a file whose payload the parser refused."""
    body = data[14:] if head["decompressedSize"] is not None else data[10:]
    if head["compressedLzx"]:
        # The decoder answers (payload, statistics); only the payload is wanted here.
        body = xnb_conformance.lzx_decompress(body, head["decompressedSize"], "<inventory>")[0]
    elif head["compressedLz4"]:
        body = xnb_conformance.lz4_block_decompress(body, head["decompressedSize"], "<inventory>")
    cursor = xnb_conformance.Cursor(body, "<inventory>")
    count = cursor.seven_bit_int()
    names, versions = [], []
    for _ in range(count):
        names.append(cursor.string())
        versions.append(cursor.i32())
    shared = cursor.seven_bit_int()
    return {
        "typeReaderNames": names,
        "typeReaders": [xnb_conformance.strip_assembly(n) for n in names],
        "typeReaderVersions": versions,
        "sharedResourceCount": shared,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=DEFAULT_ROOT)
    parser.add_argument("--out", required=True)
    parser.add_argument("--workers", type=int, default=3)
    parser.add_argument("--cache", default=None)
    args = parser.parse_args(argv)

    root = os.path.abspath(args.root)
    monogame = monogame_destinations(root)
    candidates, foreign = [], {}
    for directory, subdirs, files in os.walk(root):
        subdirs.sort()
        for name in sorted(files):
            if not name.lower().endswith(".xnb"):
                continue
            relative = os.path.relpath(os.path.join(directory, name), root)
            provenance, genuine = classify(relative, monogame)
            if genuine:
                candidates.append((relative, provenance))
            else:
                foreign[provenance] = foreign.get(provenance, 0) + 1

    cache_path = args.cache or (os.path.splitext(args.out)[0] + ".parse-cache.json")
    cache = {}
    if os.path.exists(cache_path):
        try:
            with open(cache_path, encoding="utf-8") as handle:
                cache = json.load(handle)
        except (OSError, ValueError):
            cache = {}

    def stamp(relative):
        info = os.stat(os.path.join(root, relative))
        return "%d:%d" % (info.st_size, info.st_mtime_ns)

    fresh, todo = {}, []
    for relative, _ in candidates:
        key = relative.replace(os.sep, "/")
        cached = cache.get(key)
        if cached and cached.get("stamp") == stamp(relative):
            fresh[key] = cached
        else:
            todo.append(relative)

    entries = []
    if todo:
        with concurrent.futures.ProcessPoolExecutor(max_workers=args.workers) as pool:
            for entry, relative in zip(pool.map(one, [(root, r) for r in todo], chunksize=16), todo):
                key = relative.replace(os.sep, "/")
                fresh[key] = {"stamp": stamp(relative), "entry": entry}
    for relative, provenance in candidates:
        key = relative.replace(os.sep, "/")
        entry = dict(fresh[key]["entry"])
        entry["provenance"] = provenance
        entry["sample"] = relative.split(os.sep)[0]
        entries.append(entry)
    with open(cache_path, "w", encoding="utf-8") as handle:
        json.dump(fresh, handle)

    entries.sort(key=lambda e: e["relative"])
    by_sha = {}
    for entry in entries:
        by_sha.setdefault(entry.get("sha256"), []).append(entry["relative"])
    manifest = {
        "root": root,
        "generator": "tools/xna-sample-sweep/corpus_inventory.py",
        "counts": {
            "genuineFiles": len(entries),
            "distinctContents": len(by_sha),
            "samples": len({e["sample"] for e in entries}),
            "parsed": sum(1 for e in entries if e["status"] == "parsed"),
            "unparsed": sum(1 for e in entries if e["status"] == "unparsed"),
            "notXnb": sum(1 for e in entries if e["status"] == "not-xnb"),
            "crashed": sum(1 for e in entries if e["status"] == "crashed"),
            "excludedNonGenuine": sum(foreign.values()),
        },
        "excludedByDirectory": dict(sorted(foreign.items(), key=lambda kv: -kv[1])),
        "excludedSynthesized": SYNTHESIZED,
        "entries": entries,
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print(json.dumps(manifest["counts"], indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
