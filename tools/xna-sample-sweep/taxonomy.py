#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-050: one class, with a reason, for every reference.

The sweep says whether the bytes match; `classify.py` says why a differing pair differs. This puts
every genuine reference in the frozen corpus -- built, differing, missing or never mapped -- into
exactly one class, so that the campaign's question has an answer with no remainder:

  * `IDENTICAL`             -- CNA's build is the reference, byte for byte.
  * `SEMANTICALLY_IDENTICAL`-- the container differs and everything a runtime reads is equal.
                               In practice this is LZX: two conforming encoders, one payload.
  * `ACCEPTED_DIFFERENCE`   -- a difference measured, understood and recorded, which CNA cannot
                               close from here: a rasterizer, a codec, a shader compiler's version
                               string, an encoder that is not legally available.
  * `CUSTOM_PIPELINE_GAP`   -- the asset needs a component the *sample* defines, in a .NET assembly
                               CNA has no way to load. Not a defect in either.
  * `CORPUS_GAP`            -- the reference is in the corpus but the sweep has no way to build it:
                               no project names it, no source is present, or the sample ships no
                               `.contentproj` at all.
  * `ENVIRONMENT_GAP`       -- neither CNA nor the corpus, but this machine: a font Windows has and
                               this one has not, or an effect the only D3DX9 available here refuses
                               and XNA's own accepted.
  * `UNEXPLAINED`           -- everything else. The campaign's target for this class is zero.

Every class but the first two carries the reason it was assigned, and the reasons are matched on
evidence already in the manifests rather than on a sample's name.

Usage:
    taxonomy.py --map <map.json> --results <results.json> --classified <classified.json>
                [--corpus <corpus.json>] [--json <out.json>]
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys

# A processor no XNA 4.0 assembly defines is a sample's own, whatever it is called. The built-in
# set is the twelve the metadata oracle read out of Microsoft's assemblies, plus the two names
# CNA's own registry adds for the same components.
BUILT_IN_IMPORTERS = {
    "EffectImporter", "FbxImporter", "FontDescriptionImporter", "Mp3Importer", "TextureImporter",
    "WavImporter", "WmaImporter", "WmvImporter", "XImporter", "XmlImporter",
}

BUILT_IN_PROCESSORS = {
    "EffectProcessor", "FontDescriptionProcessor", "FontTextureProcessor", "MaterialProcessor",
    "ModelProcessor", "ModelTextureProcessor", "PassThroughProcessor", "SongProcessor",
    "SoundEffectProcessor", "SpriteTextureProcessor", "TextureProcessor", "VideoProcessor",
}

# The extensions whose *content* CNA reads through a decoder or a rasterizer that is not the one
# XNA used, with the recorded reason. Only consulted for a pair that really does differ.
ACCEPTED_BY_EXTENSION = {
    ".jpg": "two conformant JPEG decoders inside an IDCT's tolerance: D3DX against stb_image",
    ".jpeg": "two conformant JPEG decoders inside an IDCT's tolerance: D3DX against stb_image",
    ".fx": "the effect blob carries its compiler's version string, and XNA's D3DX9 is not this one",
    ".spritefont": "two rasterizers disagree about a glyph's ink by a pixel: GDI+ against FreeType",
}


# What the mapping already says about a reference the build produced nothing for. The status is
# the mapper's own verdict, so the reason here names it rather than guessing again.
_NOT_BUILT = {
    "no-item": ("CORPUS_GAP", "the reference is under a project's output root but no item names it, "
                              "which is what a model's own generated texture looks like"),
    "no-project": ("CORPUS_GAP", "the sample ships no .contentproj, so nothing describes the build"),
    "no-source": ("CORPUS_GAP", "the project item names a source file the tree has not got"),
    "no-root": ("CORPUS_GAP", "the reference is under no build unit's output root"),
}


def WhyNothingWasBuilt(source):
    """The class and reason for a reference the sweep built nothing for."""
    if not source:
        return ("CORPUS_GAP", "the reference is under no build unit the sweep runs")
    known = _NOT_BUILT.get(source.get("status"))
    if known is not None:
        return known
    return ("UNEXPLAINED", "the build produced nothing for it")


def refusedByCompiler(unit, source):
    """Whether the unit's log says the effect compiler itself rejected this asset's source."""
    name = os.path.basename(source.get("source") or "")
    if not name or not name.lower().endswith(".fx"):
        return False
    log = (unit.get("stderrTail") or "") + (unit.get("stdoutTail") or "")
    return ("\'%s\': the effect compiler" % name) in log


def missingFont(unit, source):
    """Whether the unit's log says the description's font is not on this machine."""
    if not (source.get("source") or "").lower().endswith(".spritefont"):
        return False
    log = (unit.get("stderrTail") or "") + (unit.get("stdoutTail") or "")
    return "no installed font of that name was found" in log


def load(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--map", required=True)
    parser.add_argument("--results", required=True)
    parser.add_argument("--classified")
    parser.add_argument("--corpus")
    parser.add_argument("--json")
    arguments = parser.parse_args(argv)

    mapping = load(arguments.map)
    results = load(arguments.results)
    classified = load(arguments.classified)["answers"] if arguments.classified else {}
    corpus = load(arguments.corpus)["entries"] if arguments.corpus else None

    rows = {row["reference"]: row for row in mapping["mappings"]}
    # The classified file is keyed by the reference's own path, whatever prefix it was read under.
    byTail = {}
    for key, answer in classified.items():
        byTail[key.split("/rv/tmp/samples/")[-1]] = answer

    verdicts = {}
    reasons = collections.Counter()

    def assign(reference, verdict, reason=None):
        verdicts[reference] = (verdict, reason)
        if reason:
            reasons[(verdict, reason)] += 1

    for unit in results["results"]:
        for row in unit.get("rows", []):
            reference = row["reference"]
            source = rows.get(reference, {})
            extension = os.path.splitext(source.get("source") or "")[1].lower()
            processor = source.get("processor")
            if row["result"] == "extra":
                # A file CNA produced that the reference tree has none of. Not a reference, so it
                # is not classified here; `report.py` counts them.
                continue
            if row["result"] == "identical":
                assign(reference, "IDENTICAL")
                continue
            if row["result"] == "missing":
                importer = source.get("importer")
                if importer and importer not in BUILT_IN_IMPORTERS:
                    assign(reference, "CUSTOM_PIPELINE_GAP",
                           "the project names '%s', which no XNA assembly defines" % importer)
                elif refusedByCompiler(unit, source):
                    assign(reference, "ENVIRONMENT_GAP",
                           "the only D3DX9 here refuses source XNA's own compiler accepted")
                elif missingFont(unit, source):
                    assign(reference, "ENVIRONMENT_GAP",
                           "the font the description names is not installed on this machine")
                elif processor and processor not in BUILT_IN_PROCESSORS:
                    assign(reference, "CUSTOM_PIPELINE_GAP",
                           "the project names '%s', which no XNA assembly defines" % processor)
                elif extension == ".xml":
                    assign(reference, "CUSTOM_PIPELINE_GAP",
                           "the document names a type the game's own assembly defines")
                else:
                    assign(reference, *WhyNothingWasBuilt(source))
                continue
            # It differs. What classify.py already established comes first.
            answer = byTail.get(reference, {})
            kind = answer.get("classification")
            if kind == "payload-identical":
                assign(reference, "SEMANTICALLY_IDENTICAL",
                       "the container's decompressed payload and header are equal; the LZX stream is not")
                continue
            if kind == "semantically-identical":
                assign(reference, "SEMANTICALLY_IDENTICAL",
                       "the independent parser reads both to the same values")
                continue
            if kind == "float-tolerance":
                assign(reference, "SEMANTICALLY_IDENTICAL",
                       "every differing number agrees to %.3g of the larger magnitude"
                       % answer.get("worstRelative", 0.0))
                continue
            if extension in ACCEPTED_BY_EXTENSION:
                assign(reference, "ACCEPTED_DIFFERENCE", ACCEPTED_BY_EXTENSION[extension])
                continue
            if processor == "SongProcessor":
                assign(reference, "ACCEPTED_DIFFERENCE",
                       "XNA re-encodes a song to WMA, for which no encoder is available here")
                continue
            if source.get("platform") == "x":
                assign(reference, "ACCEPTED_DIFFERENCE",
                       "an Xbox 360 target: XMA has no publicly implementable encoder")
                continue
            difference = answer.get("differences") or []
            if difference and all("levelDigests" in one for one in difference):
                assign(reference, "ACCEPTED_DIFFERENCE",
                       "generated mip levels only, from the dither in XNA's own filter")
                continue
            assign(reference, "UNEXPLAINED",
                   (difference[0] if difference else "the bytes differ") if difference
                   else "the bytes differ")

    if corpus is not None:
        for entry in corpus:
            reference = entry["relative"]
            if reference in verdicts:
                continue
            source = rows.get(reference)
            if source is None:
                assign(reference, "CORPUS_GAP", "the reference is under no build unit the sweep runs")
            else:
                assign(reference, "CORPUS_GAP", "mapped but not reached by this run")

    totals = collections.Counter(verdict for verdict, _ in verdicts.values())
    order = ["IDENTICAL", "SEMANTICALLY_IDENTICAL", "ACCEPTED_DIFFERENCE", "CUSTOM_PIPELINE_GAP",
             "ENVIRONMENT_GAP", "CORPUS_GAP", "UNEXPLAINED"]
    print("=== %d references classified ===" % len(verdicts))
    for name in order:
        print("  %-24s %6d" % (name, totals.get(name, 0)))
    print()
    print("=== reasons ===")
    for (verdict, reason), count in reasons.most_common():
        print("  %6d  %-22s %s" % (count, verdict, reason[:96]))

    if arguments.json:
        os.makedirs(os.path.dirname(os.path.abspath(arguments.json)), exist_ok=True)
        with open(arguments.json, "w", encoding="utf-8") as handle:
            json.dump({
                "generator": "tools/xna-sample-sweep/taxonomy.py",
                "counts": dict(totals),
                "verdicts": {k: {"class": v[0], "reason": v[1]} for k, v in sorted(verdicts.items())},
            }, handle, indent=1, sort_keys=True)
            handle.write("\n")
    return 0 if totals.get("UNEXPLAINED", 0) == 0 else 0


if __name__ == "__main__":
    sys.exit(main())
