#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-204: does an accepted difference's evidence prove its reason?

`ACCEPTED_DIFFERENCE` is the one class with no gate under it: a reference lands there because its
*source extension* or its *processor* matched, and nothing then checks that what actually differs
is what the reason names. That is how a class becomes a graveyard, so this is the check.

Each reason gets an allow-list of field paths. A reference whose differences stay inside it is
proved; a reference with a difference the reason cannot account for is reported, with the field.
The exit status is nonzero when any is found, so the gate can be run.

    python3 tools/xna-sample-sweep/accepted_audit.py --taxonomy <taxonomy.json>
                                                     --classified <classified.json>
                                                     [--json <out.json>]
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

# One entry per accepted reason, keyed by a distinctive fragment of the reason text. `allow` is a
# list of regular expressions a differing field's path must match; `note` says what the rule is
# and why it is the right one for that reason.
RULES = [
    (r"generated mip levels", [r"^root/(?:atlas/)?levelDigests\[[1-9][0-9]*\]$"],
     "only levels the pipeline generated may differ; level 0 is the source image and a "
     "difference there is not the filter's dither"),
    (r"rasterizers disagree about a glyph's ink",
     [r"^root/(?:atlas/)?levelDigests\[\d+\]$", r"^root/cropping\[\]", r"^root/glyphs\[\]",
      r"^root/atlas/(?:width|height)$", r"^root/kerning\[\]",
      r"^root/atlas/levelByteSizes\[\]$"],
     "a glyph's ink and the box it sits in may differ, and with them the sheet they are packed "
     "into -- a different width or height is a different number of bytes, so `levelByteSizes` is "
     "the same fact as `atlas/width` and not a second one. The font's own metrics -- line "
     "spacing, spacing, the default character, the character map -- may not differ, and neither "
     "may the atlas's surface format"),
    (r"block compressors choose different endpoints",
     [r"^root/(?:atlas/)?levelDigests\[\d+\]$"],
     "only the compressed blocks may differ; a size, a format or a mip count is not a "
     "compressor's choice"),
    (r"JPEG decoders inside an IDCT", [r"^root/(?:atlas/)?levelDigests\[\d+\]$"],
     "only the decoded pixels may differ; a size, a format or a mip count is not a decoder's "
     "tolerance"),
    (r"compiler's version string", [r"^root/(?:bytecode|digest|bytecodeByteCount)"],
     "only the compiled blob may differ (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-201` "
     "records what is inside it)"),
    (r"XMA has no publicly implementable encoder",
     [r"^root/(?:digest|samples|sampleDigest|sampleByteCount|dataLength|formatTag|bitsPerSample|"
      r"blockAlign|averageBytesPerSecond|extensionData|extensionByteCount|sampleRate|channels|"
      r"durationMs|loopStart|loopLength)"],
     "an Xbox target's audio payload and the WAVEFORMATEX describing it may differ -- including "
     "`cbSize`, which is `extensionByteCount` and is 34 for an XMA format and 0 for the PCM one "
     "CNA writes instead; nothing outside that structure"),
    (r"re-encodes a song to WMA",
     [r"^root/(?:digest|duration|durationMs|mediaPath|streamReference|size)"],
     "the re-encoded media and what describes it may differ"),
]

# Fields that describe the *container* rather than the asset: how long the payload is and how the
# compressor framed it. They are not a mechanism of their own -- a payload the reason allows to
# differ takes them with it whenever it differs in length -- so they are permitted, and only when
# every other field is. A reference whose sole complaint is one of these has nothing to prove
# beyond what its reason has already proved (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-215`).
DERIVED = [re.compile(one) for one in (
    r"^decompressedLength$", r"^compressedLength$", r"^lzxFrames\[\]/", r"^lz4Frames\[\]/")]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--taxonomy", required=True)
    parser.add_argument("--classified", required=True)
    parser.add_argument("--json", default=None)
    args = parser.parse_args(argv)

    with open(args.taxonomy, encoding="utf-8") as handle:
        verdicts = json.load(handle)["verdicts"]
    with open(args.classified, encoding="utf-8") as handle:
        answers = json.load(handle)["answers"]
    byTail = {key.split("/rv/tmp/samples/")[-1]: value for key, value in answers.items()}

    compiled = [(re.compile(fragment), [re.compile(one) for one in allow], note)
                for fragment, allow, note in RULES]
    counts = collections.Counter()
    unmatched = collections.Counter()
    violations = []
    for reference, verdict in sorted(verdicts.items()):
        if verdict.get("class") != "ACCEPTED_DIFFERENCE":
            continue
        reason = verdict.get("reason") or ""
        rule = next((entry for entry in compiled if entry[0].search(reason)), None)
        if rule is None:
            unmatched[reason] += 1
            continue
        counts[rule[0].pattern] += 1
        answer = byTail.get(reference) or {}
        # `differenceFields` is the whole set of paths, with indices stripped; the listed
        # differences are capped at twelve and cannot prove anything on their own. A record
        # written before the field set existed says so rather than counting as proved.
        fields = answer.get("differenceFields")
        if fields is None:
            differences = answer.get("differences") or []
            if answer.get("differenceCount", len(differences)) > len(differences):
                violations.append({"reference": reference, "reason": reason,
                                   "field": "(no field set, and the listed differences are cut)",
                                   "difference": "%d differences recorded, %d listed"
                                                 % (answer.get("differenceCount", 0),
                                                    len(differences)),
                                   "rule": rule[0].pattern})
                continue
            fields = [one.split(":", 1)[0].strip() for one in differences]
        semantic = [one for one in fields
                    if not any(pattern.match(one) for pattern in DERIVED)]
        for field in semantic:
            if any(pattern.match(field) for pattern in rule[1]):
                continue
            violations.append({"reference": reference, "reason": reason, "field": field,
                               "difference": field, "rule": rule[0].pattern})
            break
        else:
            # Nothing semantic is left over. A container field alone is still a violation when it
            # is the *only* thing that differs, because then the payload did not differ at all and
            # the reason explains nothing.
            if not semantic and fields:
                violations.append({"reference": reference, "reason": reason, "field": fields[0],
                                   "difference": "only container framing differs; the payload "
                                                 "the reason names does not",
                                   "rule": rule[0].pattern})

    print("=== accepted differences, by reason ===")
    for pattern, count in counts.most_common():
        bad = sum(1 for one in violations if one["rule"] == pattern)
        print("  %-44s %5d proved, %d with a field the reason cannot account for"
              % (pattern[:44], count - bad, bad))
    for reason, count in unmatched.most_common():
        print("  %-44s %5d NO RULE -- the reason has no predicate here" % (reason[:44], count))
    if violations:
        print("\n=== the fields no reason accounts for ===")
        for one in violations[:40]:
            print("  %-84s %s" % (one["reference"][-84:], one["difference"][:90]))
    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({"generator": "tools/xna-sample-sweep/accepted_audit.py",
                       "counts": dict(counts), "unmatched": dict(unmatched),
                       "violations": violations}, handle, indent=1, sort_keys=True)
    return 1 if (violations or unmatched) else 0


if __name__ == "__main__":
    raise SystemExit(main())
