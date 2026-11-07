#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Verify Microsoft 3.6 authored-tail bank ownership against the exact probe inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

DLL_HASHES = {
    "x86": "8a7e7c5ac2e6230f0249d46751522e7ecf85e7490cf7491ab73bf2e7e59e4c0f",
    "x64": "6e656d37eadec5acf8ca99f2df37768eaec1c919e616caf3805c0884b2af7c26",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(matrix, corpus, source):
    rows = json.loads(matrix.read_text())
    expected = {(arch, profile) for arch in DLL_HASHES for profile in ("cue_fail", "cat_fail_tail")}
    actual = [(row["arch"], row["profile"]) for row in rows]
    require(len(actual) == len(expected) and set(actual) == expected,
            "matrix must contain each of the four architecture/profile cells once")
    for row in rows:
        label = f"{row['arch']}/{row['profile']}"
        require(row["rc"] == 0, f"{label}: native experiment failed")
        require(row["source_sha256"] == digest(source), f"{label}: probe source differs")
        require(row["dll_sha256"] == DLL_HASHES[row["arch"]], f"{label}: engine differs")
        require(set(row["banks_sha256"]) == {"clock.xgs", "clock.xsb", "clock.xwb"},
                f"{label}: incomplete bank identity")
        for name, value in row["banks_sha256"].items():
            require(digest(corpus / row["profile"] / name) == value, f"{label}: input differs: {name}")
        stages = row["stages"]
        for stage in stages:
            for key, value in stage.items():
                if key.endswith("hr"):
                    require(value == "00000000", f"{label}: unexpected HRESULT {key}={value}")
        banks = {s["bank_snapshot"]: s for s in stages if "bank_snapshot" in s}
        cues = {s["snapshot"]: s for s in stages if "snapshot" in s}
        require(banks["initial"]["sound_state"] == 0 and banks["initial"]["wave_state"] == 4,
                f"{label}: banks must start unused")
        require(cues["L_settled"]["instances"] == 1, f"{label}: L must first occupy capacity")
        tail = cues["L_authored_after20ms"]
        require(tail["instances"] == 0 and len(tail["actors"]) == 1
                and tail["actors"][0]["name"] == "L" and tail["actors"][0]["state"] == 16,
                f"{label}: require the sole Stopping/count-zero cue before I is prepared")
        ownership = banks["L_authored_after20ms"]
        require(ownership["sound_state"] == 128, f"{label}: SoundBank must retain INUSE")
        require(ownership["wave_state"] == 132, f"{label}: WaveBank must retain INUSE")
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("matrix", type=Path)
    repo = Path(__file__).resolve().parents[2]
    parser.add_argument("--corpus", type=Path, default=repo / "modules/audio/tests/CNA/Internal/Audio/instance-count")
    parser.add_argument("--source", type=Path, default=repo / "tools/xna-oracle/XactPreparedCueProbe.cpp")
    args = parser.parse_args()
    try:
        count = check(args.matrix, args.corpus, args.source)
    except (ValueError, KeyError, OSError, TypeError) as error:
        print(f"XACT tail bank oracle check failed: {error}", file=sys.stderr)
        return 1
    print(f"XACT tail bank oracle: {count} positive cells checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
