#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-021: what actually produced each sample's reference.

A sample's `.contentproj` says what the game's own build did. What produced the `.xnb` files in the
artefact root is the sample's `scripts/XnaPipelineRunner.cs`, a C# program that drives the genuine
`BuildContent` task, and the two do not always agree: most of those runners hand-list their assets
with an importer, a processor and a name and **no `ProcessorParameters` at all**, so the reference
was built with the processor's own defaults even where the project declares otherwise.

Particles3D is the case that shows it. Its project asks for `GenerateMipmaps=True`,
`TextureFormat=DxtCompressed` and `PremultiplyAlpha=False`; its runner passes none of them, and
XNA's `smoke.xnb` is 256x256 `Color` with one mip. Built with the project's parameters CNA answers
a nine-mip DXT5 -- correctly, for the project -- and built with the defaults it is byte for byte
the reference.

So this reads each runner and answers, per sample, which of three things it is:

  * **project-faithful** -- it loads the `.contentproj` and copies every item's metadata, so the
    project is the description of the build;
  * **enumerated** -- it walks the source directory, so every asset took its route from its
    extension and no parameters were set;
  * **explicit** -- it hand-lists assets, so only the `ProcessorParameters_*` it sets itself apply.

Reads only; writes nothing outside the sweep workspace.
"""
from __future__ import annotations

import argparse
import json
import os
import re

SCRIPT_NAMES = ("XnaPipelineRunner.cs", "XnaPipelineRunnerWin7.cs", "XnaPipelineRunnerWin7Local.cs",
                "DiagPipelineRunner.cs", "Xna4DiagnosticPipeline.cs")

_METADATA = re.compile(r'SetMetadata\(\s*"ProcessorParameters_([A-Za-z0-9_]+)"\s*,\s*"([^"]*)"\s*\)')
_STRING = re.compile(r'"([^"\\]*(?:\\.[^"\\]*)*)"')
_SOURCE_LIKE = re.compile(r'^[^"]*\.[A-Za-z0-9]{1,10}$')


def classify(text):
    if ".contentproj" in text and ("XmlDocument" in text or "SelectNodes" in text or
                                   "XDocument" in text):
        return "project-faithful"
    if "Directory.GetFiles" in text or "EnumerateFiles" in text:
        return "enumerated"
    return "explicit"


def overrides(text):
    """`ProcessorParameters_*` a runner sets, keyed by the source file the nearest literal names."""
    found = {}
    for match in _METADATA.finditer(text):
        # The asset a metadata call belongs to is the last source-looking string literal before it,
        # which is how every one of these runners is written: a helper takes the file name and then
        # sets the metadata on the item it made.
        before = text[:match.start()]
        source = None
        for literal in _STRING.finditer(before):
            value = literal.group(1)
            if _SOURCE_LIKE.match(value) and "\\n" not in value:
                source = value
        if source is None:
            continue
        key = source.replace("\\\\", "/").replace("\\", "/")
        found.setdefault(key, {})[match.group(1)] = match.group(2)
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default="/rv/tmp/samples")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    document = {"root": os.path.abspath(args.root),
                "generator": "tools/xna-sample-sweep/runner_provenance.py",
                "samples": {}}
    for sample in sorted(os.listdir(args.root)):
        scripts = os.path.join(args.root, sample, "scripts")
        if not os.path.isdir(scripts):
            continue
        chosen = None
        for name in SCRIPT_NAMES:
            if os.path.isfile(os.path.join(scripts, name)):
                chosen = name
                break
        if chosen is None:
            continue
        with open(os.path.join(scripts, chosen), encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        document["samples"][sample] = {
            "script": "scripts/" + chosen,
            "kind": classify(text),
            "parameterOverrides": overrides(text),
        }
    counts = {}
    for entry in document["samples"].values():
        counts[entry["kind"]] = counts.get(entry["kind"], 0) + 1
    document["counts"] = counts
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print(json.dumps({"samples": len(document["samples"]), **counts}, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
