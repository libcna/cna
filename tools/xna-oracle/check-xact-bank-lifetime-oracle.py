#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Verify retained Microsoft 3.6 cue/sound-bank ownership measurements and inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

DLLS = {
    "x86": "8a7e7c5ac2e6230f0249d46751522e7ecf85e7490cf7491ab73bf2e7e59e4c0f",
    "x64": "6e656d37eadec5acf8ca99f2df37768eaec1c919e616caf3805c0884b2af7c26",
}
MODES = {
    "staggered": {"keep", "destroy-finished", "stop", "no-props", "unplayed-stop",
                  "unplayed-destroy", "retryB", "missingA", "missingB"},
    "simple": {"keep", "unplayed-destroy", "missingA", "missingB"},
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(matrix, corpus, source):
    rows = json.loads(matrix.read_text())
    expected = {(arch, profile, mode) for arch in DLLS for profile, modes in MODES.items() for mode in modes}
    actual = [(r["arch"], r["profile"], r["mode"]) for r in rows]
    require(len(actual) == len(expected) and set(actual) == expected, "require all26 cells once")
    source_hash = digest(source)
    for row in rows:
        arch, profile, mode = row["arch"], row["profile"], row["mode"]
        label = f"{arch}/{profile}/{mode}"
        negative = mode == "missingA" or (mode == "missingB" and profile == "staggered")
        require(row["rc"] == (2 if negative else 0), f"{label}: unexpected process outcome")
        require(row["source_sha256"] == source_hash, f"{label}: different source")
        require(row["dll_sha256"] == DLLS[arch], f"{label}: different engine")
        require(set(row["banks_sha256"]) == {"lifetime.xgs", "lifetime.xsb", "A.xwb", "B.xwb"},
                f"{label}: incomplete bank identity")
        for name, value in row["banks_sha256"].items():
            require(digest(corpus / profile / name) == value, f"{label}: different bank: {name}")
        stages = row["stages"]
        snaps = {s["snapshot"]: s for s in stages if "snapshot" in s}
        first = snaps["initial"]
        require(first["sound_state"] == 0 and first["cue_state"] == 0 and first["instances"] == 0,
                f"{label}: require an initially unused sound bank")
        require(first["A_state"] == (0 if mode == "missingA" else 4), f"{label}: initial A")
        require(first["B_state"] == (0 if mode in {"missingB", "retryB"} else 4), f"{label}: initial B")
        for stage in stages:
            for key, value in stage.items():
                if not key.endswith("hr"):
                    continue
                expected_hr = "00000000"
                if key == "prepare_hr" and (negative or mode == "retryB"):
                    expected_hr = "8ac70013"
                elif key in {"sound_hr", "properties_hr"} and stage.get("snapshot") == "soundbank_destroyed":
                    expected_hr = "00000001"
                elif key == "cue_hr" and stage["cue_state"] == 0:
                    expected_hr = "00000001"
                elif key in {"A_hr", "B_hr"} and stage[key[0] + "_state"] == 0:
                    expected_hr = "00000001"
                require(value == expected_hr, f"{label}: native result {key}={value}, expected {expected_hr}")
        prepares = [s for s in stages if "prepare_hr" in s]
        require(len(prepares) == 1, f"{label}: initial Prepare identity")
        if negative or mode == "retryB":
            require(prepares[0]["prepare_hr"] == "8ac70013" and not prepares[0]["has_cue"],
                    f"{label}: expected NoWaveBank without a cue")
            failed = snaps["prepare_failed"]
            require(failed["cue_state"] == 0 and failed["instances"] == 0 and failed["sound_state"] == 0,
                    f"{label}: failed Prepare cannot own a cue")
            require(failed["A_state"] == (0 if mode == "missingA" else 132),
                    f"{label}: retain a previously resolved A even on partial failure")
            require(failed["B_state"] == (4 if mode == "missingA" else 0), f"{label}: failed B state")
            if negative:
                continue
            retry = [s for s in stages if "prepare_retry_hr" in s]
            require(len(retry) == 1 and retry[0]["prepare_retry_hr"] == "00000000" and retry[0]["has_cue"],
                    f"{label}: retry after loading B must succeed")
        else:
            require(prepares[0]["prepare_hr"] == "00000000" and prepares[0]["has_cue"],
                    f"{label}: successful Prepare required")
        b_cached = 132 if profile == "staggered" else (0 if mode == "missingB" else 4)
        for name in ["prepared", "prepared_wait"]:
            snap = snaps[name]
            require((snap["cue_state"], snap["instances"], snap["sound_state"], snap["A_state"], snap["B_state"])
                    == (4, 0, 128, 132, b_cached), f"{label}: prepared ownership: {name}")
        for name in ["phase_250", "phase_600"]:
            snap = snaps[name]
            empty = mode == "unplayed-destroy"
            playing = profile == "staggered" and mode not in {"stop", "unplayed-stop", "unplayed-destroy"}
            require((snap["cue_state"], snap["instances"], snap["sound_state"], snap["A_state"], snap["B_state"])
                    == (0 if empty else 8 if playing else 32, 1 if playing else 0,
                        0 if empty else 128, 132, b_cached), f"{label}: ownership between wave deadlines: {name}")
        end = snaps["phase_1700"]
        empty = mode == "unplayed-destroy"
        require((end["cue_state"], end["instances"], end["sound_state"], end["A_state"], end["B_state"])
                == (0 if empty else 32, 0, 0 if empty else 128, 132, b_cached),
                f"{label}: stopped/never-played ownership")
        for name in ["cue_destroyed_50", "cue_destroyed_2000"]:
            snap = snaps[name]
            require((snap["cue_state"], snap["instances"], snap["sound_state"], snap["A_state"], snap["B_state"])
                    == (0, 0, 0, 132, b_cached), f"{label}: cue destruction must retain sound-bank wave use")
        final = snaps["soundbank_destroyed"]
        require(final["A_state"] == 4 and final["B_state"] == (0 if mode == "missingB" else 4),
                f"{label}: sound-bank destruction must release cached bank use")
        if mode == "destroy-finished":
            require(any(s.get("destroy_A_skipped") and s["state"] == 132 for s in stages),
                    f"{label}: never destroy a bank still reporting INUSE")
            require(not any(s.get("stage") == "destroy_A" for s in stages),
                    f"{label}: destruction precondition is not met")
        if mode == "no-props":
            require(not any(s.get("stage") == "wave_properties" for s in stages),
                    f"{label}: metadata getter must really be omitted")
        else:
            for wave in [s for s in stages if "wave" in s]:
                require(wave["samples"] == (2205 if wave["wave"] == "A" else 22050),
                        f"{label}: authored sample count")
    return len(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("matrix", type=Path)
    repo = Path(__file__).resolve().parents[2]
    parser.add_argument("--corpus", type=Path, default=repo / "modules/audio/tests/CNA/Internal/Audio/bank-lifetime")
    parser.add_argument("--source", type=Path, default=repo / "tools/xna-oracle/XactBankLifetimeProbe.cpp")
    args = parser.parse_args()
    try:
        count = check(args.matrix, args.corpus, args.source)
    except (ValueError, KeyError, OSError, TypeError) as error:
        print(f"XACT bank lifetime oracle check failed: {error}", file=sys.stderr)
        return 1
    print(f"XACT bank lifetime oracle: {count} cells;20 positives and6 expected NoWaveBank failures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
