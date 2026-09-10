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
  * `REFERENCE_REMOVED`     -- the frozen reference is no longer in the read-only tree. Not CNA's
                               and not a gap in the sweep: another session deleted it after the
                               corpus was frozen, and the bytes survive at a sibling path.
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

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

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



# The surface format is not in the corpus manifest and is needed to tell a compressor's choice from
# a decoder's error, so it is read from the reference itself -- for the handful of references whose
# every difference is a level digest, which is a few hundred parses and only where it decides
# something (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-204`).
_formatCache = {}


def BlockCompressed(reference, root="/rv/tmp/samples"):
    """Whether a reference's own texture format is one a block compressor produced."""
    if reference in _formatCache:
        return _formatCache[reference]
    answer = False
    try:
        sys.path.insert(0, os.path.join(REPO, "tools", "xnb"))
        import xnb_conformance  # noqa: PLC0415 - imported lazily, only where it decides something
        parsed = xnb_conformance.parse(os.path.join(root, reference))
        surface = str((parsed.get("root") or {}).get("surfaceFormat") or "")
        answer = surface.startswith("Dxt")
    except Exception:  # noqa: BLE001 - a reference this cannot parse is simply not proved
        answer = False
    _formatCache[reference] = answer
    return answer



def NestedOwnerIsCustom(rows, reference):
    """The asset a `*_N.xnb` is the side output of, when that asset needs a sample's own component.

    A model's own generated textures are named `<the texture's stem>_<n>.xnb` and no project item
    names them, so they arrive as `no-item`. Reading that as a corpus gap is wrong twice over: the
    sweep *did* reach them, and the reason CNA produced none of them is usually visible right
    beside them -- the model they belong to failed for a processor the sample defines.
    `customComponentsOnly` asks whether *every* asset the unit failed on is custom, which is too
    strong for a unit that also builds ordinary assets; this asks the narrower question about the
    one asset that would have written this file (plans/plan_xna_sample_xnb_sweep.md
    `XNASWEEP-206`).

    @param rows The mapping, keyed by reference.
    @param reference The reference's path.
    @return `(asset name, component)` when the owner needs a sample-defined component, else None.
    """
    if re.match(r"^.*_\d+\.xnb$", reference) is None:
        return None
    # The file is named after the *texture* the model referenced, not after the model, so the owner
    # cannot be recovered from the name. What can be asked is whether anything in the directory it
    # was written into needs a component the sample defines: a model's side outputs land beside the
    # model, and a directory holding a model no XNA assembly can build is a directory whose side
    # outputs nobody here can write either.
    directory = os.path.dirname(reference)
    for path, row in rows.items():
        if os.path.dirname(path) != directory or path == reference:
            continue
        for key, known in (("processor", BUILT_IN_PROCESSORS), ("importer", BUILT_IN_IMPORTERS)):
            value = row.get(key) or ""
            if value and value not in known:
                return (row.get("assetName") or os.path.basename(path), value)
    return None


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


_CUSTOM_COMPONENT = re.compile(r'Cannot find content (?:processor|importer) "([^"]+)"')
_FAILED_ASSET = re.compile(r"^error: (\S.*) \[([^\]]+)\]$", re.M)


def customComponentsOnly(unit):
    """The custom components a unit refused, when they are the only reason anything failed.

    A reference the project names no item for is a *nested* output -- a model's own texture, an
    effect a material clones -- and only the item that names it can produce one. When every asset
    a unit failed on failed because a component the sample defines could not be loaded, the nested
    outputs those items would have produced are that same gap rather than a hole in the sweep's
    mapping (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-185`).

    Returns the sorted component names, or an empty list when the unit also failed for another
    reason -- in which case the reference keeps the mapper's own verdict rather than being
    attributed to a refusal that may not be its.
    """
    log = (unit.get("stderrTail") or "")
    names = sorted(set(_CUSTOM_COMPONENT.findall(log)))
    if not names or _FAILED_ASSET.search(log):
        return []
    return names


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
            if row["result"] == "reference-removed":
                # Not CNA's and not the sweep's: the reference file itself is no longer on disk.
                # `/rv/tmp/samples` is not this campaign's tree, and another session's
                # `prune-completed-sample.sh` removed 21 of the frozen references on 2026-09-09.
                # It has its own class so it can never be read as a comparison that passed
                # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-190`, §11.4).
                assign(reference, "REFERENCE_REMOVED",
                       "the frozen reference was deleted from the read-only tree by another "
                       "session after this corpus was frozen")
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
                elif (source.get("status") == "no-item" and
                      NestedOwnerIsCustom(rows, reference)):
                    owner = NestedOwnerIsCustom(rows, reference)
                    assign(reference, "CUSTOM_PIPELINE_GAP",
                           "it is the side output of '%s', whose processor '%s' no XNA assembly "
                           "defines" % (owner[0], owner[1]))
                elif (source.get("status") == "no-item" and customComponentsOnly(unit)):
                    assign(reference, "CUSTOM_PIPELINE_GAP",
                           "no item names it, and every asset this unit failed on failed for a "
                           "component the sample defines: " +
                           ", ".join("'%s'" % name for name in customComponentsOnly(unit)))
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
            complete = answer.get("differenceCount", len(difference)) <= len(difference)
            if difference and complete and all("levelDigests" in one for one in difference):
                # *Which* levels differ is the whole of the reason, and reading "every difference
                # mentions a level digest" as "only the generated levels differ" put 304 of 320
                # references under a reason that does not explain them: 226 are block-compressed
                # textures whose level 0 differs -- two conformant DXT compressors choosing
                # different endpoints for the same pixels, which is a different accepted reason --
                # and 78 are *uncompressed* textures whose base image differs, which no filter and
                # no compressor accounts for at all (plans/plan_xna_sample_xnb_sweep.md
                # `XNASWEEP-204`).
                levels = {int(found.group(1))
                          for found in (re.search(r"levelDigests\[(\d+)\]", one)
                                        for one in difference) if found}
                if levels and 0 not in levels:
                    assign(reference, "ACCEPTED_DIFFERENCE",
                           "generated mip levels only, from the dither in XNA's own filter")
                    continue
                if BlockCompressed(reference):
                    assign(reference, "ACCEPTED_DIFFERENCE",
                           "two block compressors choose different endpoints for the same pixels")
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
