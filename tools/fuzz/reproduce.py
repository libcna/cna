#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""MTH-SAFE-007: replay one kept long-campaign finding outside the campaign driver.

It runs the harness's own single-input mode -- ``<harness> --replay-one <input>`` -- under the
sanitizer options, per-candidate timeout and RSS limit the campaign recorded, and prints that
exact command, which also works by hand. Exit status: 1 while the finding still reproduces, 0 once
it no longer does (the input runs to the harness's result line), 2 when nothing could be judged
(missing binary or input, a harness failing before its start line, a corpus that no longer matches
the recorded hashes, a regenerated input that differs from the kept one).

  --regenerate  first rebuild the input from the recorded seed with ``--emit`` over the recorded
                corpus and require the same SHA-256 as the kept input: the seed is exact;
  --minimize F  shrink the input while the same kind of failure (and sanitizer) still occurs, and
                write the smaller input to F.

A failure at exit that did not bisect to one seed is kept as a seed range, and one that a batch
of zero candidates already shows as input-independent; both are replayed with
``<harness> --seeded <first> <count> <corpus>...`` over the recorded corpus.

A kept artifact is not a regression test. Once understood, copy the minimized input into the
harness's deterministic corpus and replay it from the matching test -- see "Scheduled long
campaign" in docs/fx-bytecode-fuzzing.md.
"""

from __future__ import annotations

import argparse
import json
import shlex
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import run_long_campaign as campaign  # noqa: E402

JUDGED, REPRODUCES, UNJUDGED = 0, 1, 2


def environment(record: dict) -> dict:
    """The caller's environment, with the recorded sanitizer options where the caller set none."""
    extra = {key: value for key, value in record.get("environment", {}).items()
             if key not in campaign.os.environ}
    return campaign.child_environment(extra)


def corpus_paths(record: dict) -> list[str] | None:
    paths = []
    for entry in record.get("corpus", []):
        path = Path(entry["path"])
        path = path if path.is_absolute() else campaign.REPO_ROOT / path
        if not path.is_file() or campaign.sha256_file(path) != entry["sha256"]:
            print(f"corpus changed or missing since the finding: {entry['path']}", file=sys.stderr)
            return None
        paths.append(str(path))
    return paths


def reproduced(outcome: campaign.Outcome) -> bool:
    return outcome.kind not in ("ok", "startup", "capability")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("finding", type=Path, help="a findings/<kind>-seed-<seed>.json record")
    parser.add_argument("--binary", type=Path, help="harness binary (default: the recorded one)")
    parser.add_argument("--input", type=Path, help="input to replay (default: the kept one)")
    parser.add_argument("--timeout", type=float, help="per-candidate timeout (default: recorded)")
    parser.add_argument("--rss-limit-mb", type=float, help="RSS limit (default: recorded)")
    parser.add_argument("--startup-timeout", type=float, default=120.0)
    parser.add_argument("--regenerate", action="store_true")
    parser.add_argument("--minimize", type=Path, metavar="OUTPUT")
    parser.add_argument("--max-runs", type=int, default=300, help="minimization replay budget")
    args = parser.parse_args(argv)

    record = json.loads(args.finding.read_text(encoding="utf-8"))
    binary = args.binary or Path(record["binary"])
    if not binary.is_file():
        print(f"harness binary not found: {binary}", file=sys.stderr)
        return UNJUDGED
    launcher = list(record.get("launcher", []))
    env = environment(record)
    timeout = args.timeout or float(record.get("candidate_timeout_seconds") or 60)
    rss_limit = args.rss_limit_mb or record.get("rss_limit_mb")

    def run(arguments: list[str]) -> campaign.Outcome:
        return campaign.supervise([*launcher, str(binary), *arguments], env, startup_timeout=args.startup_timeout,
                                  candidate_timeout=timeout, rss_limit_mb=rss_limit)

    if record.get("seed") is None:
        # A seed range, or a control batch of zero candidates for an input-independent failure.
        batch = record["seeded_replay"]
        corpus = corpus_paths(record)
        if corpus is None:
            return UNJUDGED
        arguments = ["--seeded", str(batch["first"]), str(batch["count"]), *corpus]
    else:
        source = args.input or args.finding.parent / record["input"]
        if not source.is_file():
            print(f"input not found: {source}", file=sys.stderr)
            return UNJUDGED
        if args.regenerate:
            corpus = corpus_paths(record)
            if corpus is None:
                return UNJUDGED
            regenerated = args.finding.parent / f".regenerated-{record['seed']}"
            completed = campaign.subprocess.run([*launcher, str(binary), "--emit", str(record["seed"]),
                                                 str(regenerated), *corpus], env=env, capture_output=True,
                                                timeout=300)
            digest = campaign.sha256_file(regenerated) if regenerated.is_file() else None
            regenerated.unlink(missing_ok=True)
            if completed.returncode != 0 or digest != record["input_sha256"]:
                print(f"seed {record['seed']} regenerates {digest}, not the kept {record['input_sha256']}",
                      file=sys.stderr)
                return UNJUDGED
            print(f"seed {record['seed']} regenerates the kept input exactly (sha256 {digest})")
        arguments = ["--replay-one", str(source)]

    options = " ".join(f"{key}={shlex.quote(env[key])}" for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS") if key in env)
    print("replay:", options, shlex.join([*launcher, str(binary), *arguments]))
    outcome = run(arguments)
    if outcome.kind in ("startup", "capability"):
        print(f"not judged: the harness failed before its start line (exit {outcome.returncode}):\n"
              + outcome.stderr_tail[-2000:], file=sys.stderr)
        return UNJUDGED
    if not reproduced(outcome):
        print(f"does not reproduce: the input ran to its result line in {outcome.elapsed:.1f} s")
        return JUDGED
    print(f"reproduces: {outcome.kind}, sanitizer {outcome.sanitizer}, signal {outcome.signal_name}, "
          f"exit {outcome.returncode}\n  {outcome.signature or '(no sanitizer summary)'}")
    if args.minimize and record.get("seed") is not None:
        minimize(Path(arguments[1]), args.minimize, outcome, run, args.max_runs)
    return REPRODUCES


def minimize(source: Path, output: Path, original: campaign.Outcome, run, budget: int) -> None:
    """Greedy chunk removal, halving the chunk, while the same failure keeps reproducing."""
    data = source.read_bytes()
    probe = output.with_name(output.name + ".probe")
    runs = 0

    def same(outcome: campaign.Outcome) -> bool:
        return outcome.kind == original.kind and outcome.sanitizer == original.sanitizer

    size = max(1, len(data) // 2)
    while size >= 1 and runs < budget:
        position, changed = 0, False
        while position < len(data) and runs < budget:
            candidate = data[:position] + data[position + size:]
            if not candidate:
                position += size
                continue
            probe.write_bytes(candidate)
            runs += 1
            if same(run(["--replay-one", str(probe)])):
                data, changed = candidate, True
            else:
                position += size
        if not changed:
            size //= 2
    probe.unlink(missing_ok=True)
    output.write_bytes(data)
    print(f"minimized: {source.stat().st_size} -> {len(data)} bytes in {runs} replays -> {output}")


if __name__ == "__main__":
    sys.exit(main())
