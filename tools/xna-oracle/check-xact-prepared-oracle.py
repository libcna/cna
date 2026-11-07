#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Check the retained Microsoft 3.6 preparation experiment and its exact inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

DLL_HASHES = {
    "x86": "8a7e7c5ac2e6230f0249d46751522e7ecf85e7490cf7491ab73bf2e7e59e4c0f",
    "x64": "6e656d37eadec5acf8ca99f2df37768eaec1c919e616caf3805c0884b2af7c26",
}
CASES = {
    ("cue_oldest", "before"), ("cat_oldest", "before"),
    ("cue_oldest", "evicted-only"), ("cat_oldest", "evicted-only"),
    ("cue_oldest", "replay-order"), ("cat_oldest", "replay-order"),
    ("cue_oldest_two", "reverse-play"), ("cat_oldest_two", "reverse-play"),
    ("unlimited", "none"), ("unlimited", "no-wave"),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(matrix, corpus, source):
    rows = json.loads(matrix.read_text())
    expected = {(arch, profile, mode) for arch in DLL_HASHES for profile, mode in CASES}
    actual = [(r["arch"], r["profile"], r["mode"]) for r in rows]
    require(len(actual) == len(expected) and set(actual) == expected,
            "matrix must contain each of the twenty architecture/scenario cells once")
    source_hash = digest(source)
    for row in rows:
        arch, profile, mode = row["arch"], row["profile"], row["mode"]
        label = f"{arch}/{profile}/{mode}"
        require(row["source_sha256"] == source_hash, f"{label}: probe source differs")
        require(row["dll_sha256"] == DLL_HASHES[arch], f"{label}: engine is not the pinned genuine DLL")
        for name, value in row["banks_sha256"].items():
            require(digest(corpus / profile / name) == value, f"{label}: bank input differs: {name}")
        require(set(row["banks_sha256"]) == {"clock.xgs", "clock.xsb", "clock.xwb"},
                f"{label}: incomplete bank identity")
        stages = row["stages"]
        banks = {s["bank_snapshot"]: s for s in stages if "bank_snapshot" in s}
        cues = {s["snapshot"]: s for s in stages if "snapshot" in s}
        require(banks["initial"]["sound_state"] == 0 and cues["initial"]["instances"] == 0,
                f"{label}: initial sound bank must be unused/count zero")
        if mode == "no-wave":
            require(row["rc"] == 2, f"{label}: missing-wave probe must fail at Prepare")
            require(banks["initial"]["wave_hr"] == "00000001" and banks["initial"]["wave_state"] == 0,
                    f"{label}: negative control must omit the wave bank")
            failures = [s for s in stages if s.get("stage") == "prepare_L"]
            require(len(failures) == 1 and failures[0]["hr"] == "8ac70013",
                    f"{label}: expected the native NoWaveBank Prepare error")
            require("L_prepared" not in cues, f"{label}: failed Prepare cannot yield a cue")
            continue
        require(row["rc"] == 0, f"{label}: positive experiment failed")
        for stage in stages:
            for key, value in stage.items():
                if key.endswith("hr"):
                    require(value == "00000000", f"{label}: unexpected HRESULT {key}={value}")
        require(banks["initial"]["wave_state"] == 4, f"{label}: loaded wave bank must start Prepared")
        prepared_name = "P_prepared_before_L" if "P_prepared_before_L" in banks else "L_prepared"
        require(banks[prepared_name]["sound_state"] == 128 and banks[prepared_name]["wave_state"] == 132,
                f"{label}: preparation must retain both banks")
        require(cues[prepared_name]["instances"] == 0,
                f"{label}: preparation alone must not count as playback")
        if mode == "evicted-only":
            require(banks["only_evicted_P"]["sound_state"] == 128
                    and banks["only_evicted_P"]["wave_state"] == 132,
                    f"{label}: selected prepared handle must retain both bank resources")
            remaining = cues["only_evicted_P"]
            require(remaining["instances"] == 0 and len(remaining["actors"]) == 1
                    and remaining["actors"][0]["name"] == "P" and remaining["actors"][0]["state"] == 4,
                    f"{label}: only the unplayed prepared handle may remain")
        if mode in {"reverse-play", "replay-order"}:
            final = cues["I_settled" if mode == "reverse-play" else "J_after_P_replay"]
            actors = {a["name"]: a for a in final["actors"]}
            require(final["instances"] == 2 and actors["P"]["state"] == 32,
                    f"{label}: original preparation position must select P")
            require(actors["I"]["state"] == 8, f"{label}: I must remain Playing")
            require(actors["L"]["state"] == (8 if mode == "reverse-play" else 32),
                    f"{label}: unexpected L retirement")
            if mode == "replay-order":
                require(actors["J"]["state"] == 8, f"{label}: J must remain Playing")
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
        print(f"XACT preparation oracle check failed: {error}", file=sys.stderr)
        return 1
    print(f"XACT preparation oracle: {count} cells checked; 18 positives and 2 expected NoWaveBank failures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
