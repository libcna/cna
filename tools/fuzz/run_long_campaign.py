#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""MTH-SAFE-007: a long, interruptible fuzz campaign over CNA's standalone seeded harnesses.

Each selected target runs for a wall-time budget over an explicit, contiguous seed range. A
harness (tools/fuzz/seeded_harness.hpp) derives candidate N from its ordered corpus and seed N
alone, so a finding is one exact seed: the driver regenerates the input with ``--emit``, keeps it
under ``<output>/<target>/findings/`` and checks whether ``--replay-one`` reproduces it in
isolation. tools/fuzz/reproduce.py replays a kept finding without this driver.

Per target the driver writes ``summary.json`` (binary hash, instrumentation read from the build's
CMakeCache.txt and compile_commands.json, corpus hashes, seed range, attempts, operations and the
other counters the harness reports, elapsed time, peak RSS, timeouts, crashes, RSS failures) after
every harness process, and ``stderr.log`` (bounded). ``campaign.json`` aggregates the targets.

None of these can pass:
  * zero attempts, zero operations, or a zero required counter (no effect ever drawn, say);
  * a missing or empty corpus, a missing binary, or missing required sanitizer instrumentation;
  * a harness reporting its required capability unavailable (exit 3) or describing another one;
  * a harness failing before its start line, or reporting attempts that disagree with its seeds.

Exit status: 0 every target passed, or the run was interrupted without a finding; 1 at least one
finding (crash, sanitizer report, timeout, RSS limit, failure at exit such as a leak); 2 a rule
above failed and nothing was found. SIGINT/SIGTERM stop cleanly: each running harness finishes
its current candidate, and partial summaries are written and marked "interrupted".

Coverage is not measured: the sanitizer build carries no coverage instrumentation, and attempts
are not coverage. Each summary says so rather than leaving the field out.
"""

from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import platform
import re
import selectors
import shlex
import signal
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SCHEMA_VERSION = 1
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TARGETS = Path(__file__).resolve().with_name("campaign_targets.json")
EXIT_PASSED, EXIT_FINDINGS, EXIT_FAILED = 0, 1, 2
HARNESS_USAGE_EXIT, HARNESS_CAPABILITY_EXIT = 2, 3
MAX_SEED = (1 << 64) - 1
STDERR_TAIL_BYTES = 64 * 1024
STDERR_LOG_BYTES = 4 * 1024 * 1024
MAX_BISECTION_RUNS = 64
KILL_GRACE_SECONDS = 10.0
CMAKE_KEYS = ("CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_VERSION", "CNA_SANITIZE",
              "CNA_SANITIZE_OPTIMIZATION", "CNA_GRAPHICS_RENDERER", "CNA_SOFTWARE_COMPILED_EFFECTS",
              "CNA_PLATFORM", "CNA_BUILD_C_API", "CNA_FNA3D_GIT_TAG", "FETCHCONTENT_SOURCE_DIR_FNA3D")
ENVIRONMENT_KEYS = ("ASAN_OPTIONS", "UBSAN_OPTIONS", "LSAN_OPTIONS", "CNA_GRAPHICS_RENDERER")
COVERAGE = {"available": False,
            "reason": "the sanitizer build has no coverage instrumentation; attempts and reached "
                      "operations are measured instead, and neither is code coverage"}


def utc_now() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value) -> None:
    """Atomic, so a killed driver never leaves half a summary."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def display_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return str(path.resolve())


# --------------------------------------------------------------------------------------------
# Supervising one harness process
# --------------------------------------------------------------------------------------------

class BoundedLog:
    """A per-target stderr log that stops growing at a fixed size (log retention, not a dump)."""

    def __init__(self, path: Path | None, limit: int = STDERR_LOG_BYTES):
        self.path, self.limit, self.written, self.truncated = path, limit, 0, False
        self.lock = threading.Lock()
        self.stream = path.open("ab") if path else None

    def header(self, text: str) -> None:
        self.write(("\n==== " + text + "\n").encode())

    def write(self, data: bytes) -> None:
        if not self.stream:
            return
        with self.lock:
            room = self.limit - self.written
            if room <= 0:
                if not self.truncated:
                    self.stream.write(b"\n[stderr log limit reached; later output dropped]\n")
                    self.truncated = True
                return
            self.stream.write(data[:room])
            self.written += min(room, len(data))
            self.stream.flush()

    def close(self) -> None:
        if self.stream:
            self.stream.close()


class Outcome:
    """What one harness process did, judged from its protocol lines, exit status and stderr."""

    def __init__(self):
        self.argv: list[str] = []
        self.start: dict | None = None
        self.result: dict | None = None
        self.progress: dict | None = None
        self.last_seed: int | None = None
        self.returncode: int | None = None
        self.killed_for: str | None = None
        self.stop_sent = False
        self.elapsed = 0.0
        self.peak_rss_kb = 0
        self.rss_at_kill_kb: int | None = None
        self.stderr_tail = ""

    @property
    def signal_name(self) -> str | None:
        if self.returncode is not None and self.returncode < 0:
            try:
                return signal.Signals(-self.returncode).name
            except ValueError:
                return str(-self.returncode)
        return None

    @property
    def kind(self) -> str:
        """ok, exit, timeout, rss, crash, stopped, capability or startup."""
        if self.killed_for in ("timeout", "rss"):
            return self.killed_for
        if self.result is not None:
            return "ok" if self.returncode == 0 else "exit"
        if self.killed_for == "startup" or self.start is None:
            return "capability" if self.returncode == HARNESS_CAPABILITY_EXIT else "startup"
        if self.stop_sent and self.returncode in (-signal.SIGTERM, -signal.SIGINT):
            return "stopped"
        return "crash" if self.last_seed is not None else "startup"

    @property
    def sanitizer(self) -> str | None:
        text = self.stderr_tail
        for name, marker in (("LeakSanitizer", "ERROR: LeakSanitizer"),
                             ("AddressSanitizer", "ERROR: AddressSanitizer"),
                             ("UndefinedBehaviorSanitizer", ": runtime error:")):
            if marker in text:
                return name
        # A long leak report can push its header out of the kept tail; its SUMMARY stays.
        return "LeakSanitizer" if re.search(r"SUMMARY: \w+Sanitizer: \d+ byte\(s\) leaked", text) else None

    @property
    def signature(self) -> str | None:
        """The line a human would search for: a sanitizer SUMMARY, else std::terminate's message."""
        lines = self.stderr_tail.splitlines()
        for prefix in ("SUMMARY: ", "terminate called"):
            for line in lines:
                if line.startswith(prefix):
                    return line.strip()[:300]
        for line in lines:
            if ": runtime error:" in line:
                return line.strip()[:300]
        return None

    @property
    def dedup_signature(self) -> str | None:
        """The signature without what varies between hits of one defect.

        A leak report's byte counts and a UBSan report's values differ from hit to hit; the
        allocating frames and the source location do not.
        """
        lines = self.stderr_tail.splitlines()
        if self.sanitizer == "LeakSanitizer":
            for index, line in enumerate(lines):
                if line.startswith(("Direct leak", "Indirect leak")):
                    frames = []
                    for frame in lines[index + 1:index + 12]:
                        match = re.match(r"\s*#\d+ 0x[0-9a-f]+ in (.+?)(?: [^ ]+:\d+(?::\d+)?)?$", frame)
                        if not match:
                            break
                        name = match.group(1)
                        if not name.startswith(("std::", "__gnu_cxx::", "void std::")) and not any(
                                word in name for word in ("operator new", "malloc", "calloc", "realloc",
                                                          "SDL_malloc", "__interceptor")):
                            frames.append(name[:120])
                        if len(frames) == 3:
                            break
                    return "LeakSanitizer: " + " <- ".join(frames)
        if self.sanitizer == "UndefinedBehaviorSanitizer":
            for line in lines:
                if ": runtime error:" in line:
                    return line.split(": runtime error:", 1)[0].strip() + ": runtime error"
        return self.signature


def read_rss_kb(pid: int) -> tuple[int, int] | None:
    try:
        text = Path(f"/proc/{pid}/status").read_text()
    except OSError:
        return None
    values = {}
    for line in text.splitlines():
        if line.startswith(("VmRSS:", "VmHWM:")):
            values[line[:5]] = int(line.split()[1])
    return values.get("VmRSS", 0), values.get("VmHWM", 0)


def supervise(argv: list[str], env: dict, *, startup_timeout: float, candidate_timeout: float,
              rss_limit_mb: float | None, stop_event: threading.Event | None = None,
              deadline: float | None = None, log: BoundedLog | None = None) -> Outcome:
    """Runs one harness process with startup, per-candidate and RSS watchdogs.

    ``deadline`` and ``stop_event`` end the batch gracefully (SIGTERM: the harness finishes its
    current candidate and prints its result); the watchdogs end it with SIGKILL.
    """
    outcome = Outcome()
    outcome.argv = list(argv)
    began = time.monotonic()
    process = subprocess.Popen(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, env=env, start_new_session=True)
    tail = bytearray()
    tail_lock = threading.Lock()

    def pump_stderr():
        while True:
            data = process.stderr.read1(1 << 16)
            if not data:
                return
            if log:
                log.write(data)
            with tail_lock:
                tail.extend(data)
                if len(tail) > STDERR_TAIL_BYTES:
                    del tail[:len(tail) - STDERR_TAIL_BYTES]

    pump = threading.Thread(target=pump_stderr, daemon=True)
    pump.start()
    stdout = process.stdout.fileno()
    os.set_blocking(stdout, False)
    selector = selectors.DefaultSelector()
    selector.register(stdout, selectors.EVENT_READ)
    pending = b""
    last_progress = began
    killed_at = None
    next_rss_poll = 0.0

    def kill(reason: str) -> None:
        nonlocal killed_at
        outcome.killed_for = reason
        killed_at = time.monotonic()
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass

    def parse(line: bytes, prefix: bytes):
        try:
            return json.loads(line[len(prefix):])
        except ValueError:
            return {"unparsable": line[:200].decode(errors="replace")}

    while True:
        eof = False
        if selector.select(timeout=0.2):
            try:
                data = os.read(stdout, 1 << 16)
            except BlockingIOError:
                data = None
            if data == b"":
                eof = True
            elif data:
                block, newline, pending = (pending + data).rpartition(b"\n")
                if newline:
                    block = b"\n" + block
                    now = time.monotonic()
                    for prefix, field in ((b"cna_fuzz_start ", "start"), (b"cna_fuzz_result ", "result")):
                        position = block.find(b"\n" + prefix)
                        if position >= 0:
                            end = block.find(b"\n", position + 1)
                            setattr(outcome, field, parse(block[position + 1:end if end >= 0 else None], prefix))
                            last_progress = now
                    position = block.rfind(b"\ncna_fuzz_progress ")
                    if position >= 0:
                        end = block.find(b"\n", position + 1)
                        outcome.progress = parse(block[position + 1:end if end >= 0 else None], b"cna_fuzz_progress ")
                    position = block.rfind(b"\ncna_fuzz_begin ")
                    if position >= 0:
                        end = block.find(b"\n", position + 1)
                        outcome.last_seed = int(block[position + 16:end if end >= 0 else None])
                        last_progress = now
        if eof:
            break
        now = time.monotonic()
        if killed_at is not None:
            if now - killed_at > KILL_GRACE_SECONDS:
                break  # something else holds the pipe open; the process itself is gone
            continue
        if now >= next_rss_poll:
            next_rss_poll = now + 0.25
            rss = read_rss_kb(process.pid)
            if rss:
                outcome.peak_rss_kb = max(outcome.peak_rss_kb, rss[1])
                if rss_limit_mb and rss[0] > rss_limit_mb * 1024:
                    outcome.rss_at_kill_kb = rss[0]
                    kill("rss")
                    continue
        if outcome.start is None and outcome.result is None and now - began > startup_timeout:
            kill("startup")
        elif outcome.start is not None and outcome.result is None and now - last_progress > candidate_timeout:
            kill("timeout")
        elif not outcome.stop_sent and ((stop_event is not None and stop_event.is_set()) or
                                        (deadline is not None and now >= deadline)):
            outcome.stop_sent = True
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
    selector.close()
    _, status, usage = os.wait4(process.pid, 0)
    process.returncode = outcome.returncode = os.waitstatus_to_exitcode(status)
    pump.join(timeout=KILL_GRACE_SECONDS)
    process.stdout.close()
    process.stderr.close()
    outcome.peak_rss_kb = max(outcome.peak_rss_kb, usage.ru_maxrss)
    outcome.elapsed = time.monotonic() - began
    with tail_lock:
        outcome.stderr_tail = bytes(tail).decode(errors="replace")
    return outcome


# --------------------------------------------------------------------------------------------
# Build, corpus and configuration facts
# --------------------------------------------------------------------------------------------

def read_cmake_cache(build_dir: Path | None) -> dict:
    cache = build_dir / "CMakeCache.txt" if build_dir else None
    if not cache or not cache.is_file():
        return {}
    values = {}
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith(("#", "//")) or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key_type, value = line.split("=", 1)
        key = key_type.split(":", 1)[0]
        if key in CMAKE_KEYS:
            values[key] = value
    return values


def compile_flags(build_dir: Path | None, source: str | None) -> list[str] | None:
    """Instrumentation-relevant flags of the harness's own translation unit, or None if unknown."""
    database = build_dir / "compile_commands.json" if build_dir else None
    if not source or not database or not database.is_file():
        return None
    wanted = (REPO_ROOT / source).resolve()
    for entry in json.loads(database.read_text()):
        path = Path(entry["directory"], entry["file"]).resolve()
        if path != wanted:
            continue
        arguments = entry.get("arguments") or shlex.split(entry.get("command", ""))
        return [argument for argument in arguments
                if argument.startswith(("-fsanitize", "-fno-sanitize", "-O", "-fno-omit-frame-pointer",
                                        "-D_GLIBCXX_ASSERTIONS", "-D_FORTIFY_SOURCE"))
                or argument in ("-g", "-g1", "-g2", "-g3", "-ggdb", "--coverage", "-fprofile-arcs")]
    return None


def sanitizers_from(flags: list[str] | None) -> list[str]:
    enabled: set[str] = set()
    for flag in flags or ():
        if flag.startswith("-fsanitize="):
            enabled.update(flag.split("=", 1)[1].split(","))
        elif flag.startswith("-fno-sanitize="):
            enabled.difference_update(flag.split("=", 1)[1].split(","))
    return sorted(enabled)


def collect_corpus(specs: list[dict]) -> list[dict]:
    files: list[dict] = []
    for spec in specs:
        path = Path(spec["path"])
        path = path if path.is_absolute() else REPO_ROOT / path
        suffixes = tuple(spec.get("suffixes", ()))
        if path.is_dir():
            chosen = sorted((entry for entry in path.iterdir()
                             if entry.is_file() and (not suffixes or entry.name.endswith(suffixes))),
                            key=lambda entry: entry.name)
        elif path.is_file():
            chosen = [path]
        else:
            chosen = []
        for entry in chosen:
            files.append({"path": display_path(entry), "absolute": str(entry.resolve()),
                          "bytes": entry.stat().st_size, "sha256": sha256_file(entry)})
    return files


def child_environment(extra: dict) -> dict:
    environment = dict(os.environ)
    environment.update({key: str(value) for key, value in extra.items()})
    # A UBSan report that does not stop the process cannot be attributed to a seed.
    ubsan = environment.get("UBSAN_OPTIONS", "")
    for option in ("halt_on_error=1", "print_stacktrace=1"):
        if option.split("=")[0] + "=" not in ubsan:
            ubsan = (ubsan + ":" if ubsan else "") + option
    environment["UBSAN_OPTIONS"] = ubsan
    return environment


# --------------------------------------------------------------------------------------------
# One target
# --------------------------------------------------------------------------------------------

class TargetRun:
    def __init__(self, config: dict, args, stop_event: threading.Event):
        self.config, self.args, self.stop_event = config, args, stop_event
        self.name = config["name"]
        self.directory = args.output_dir / self.name
        self.findings_directory = self.directory / "findings"
        binary = Path(config["binary"])
        if not binary.is_absolute():
            binary = (args.build_dir or REPO_ROOT) / binary
        self.binary = binary
        self.launcher = list(config.get("launcher", ()))
        scale = args.budget_scale
        self.budget = float(args.budget_seconds if args.budget_seconds is not None
                            else config["budget_seconds"] * scale)
        self.candidate_timeout = float(config.get("candidate_timeout_seconds", 60))
        self.startup_timeout = float(config.get("startup_timeout_seconds", 120))
        self.rss_limit_mb = config.get("rss_limit_mb")
        self.chunk_seconds = float(config.get("chunk_seconds", 60))
        self.max_findings = int(config.get("max_findings", args.max_findings_per_target))
        self.keep_per_signature = int(config.get("keep_per_signature", args.keep_per_signature))
        self.repeats: dict[str, dict] = {}
        self.environment = child_environment(config.get("env", {}))
        self.recorded_environment = {key: self.environment[key] for key in
                                     (*ENVIRONMENT_KEYS, *config.get("env", {})) if key in self.environment}
        self.first_seed = args.seed_start
        self.last_seed = min(MAX_SEED, args.seed_start + args.seed_count - 1)
        self.next_seed = self.first_seed
        self.attempts = 0
        self.unaccounted_attempts = 0
        self.processes = 0
        self.counters: dict[str, int] = {}
        self.findings: list[dict] = []
        self.failures: list[str] = []
        self.capabilities: set[str] = set()
        self.peak_rss_kb = 0
        self.rate = None
        self.elapsed = 0.0
        self.status = "not_run"
        self.stop_reason = None
        self.started_at = self.finished_at = None
        self.log: BoundedLog | None = None
        self.corpus: list[dict] = []
        self.instrumentation: dict = {}
        self.binary_sha256 = None
        self.bisection_runs = 0

    # -- facts -----------------------------------------------------------------------------
    def prepare(self) -> bool:
        if not self.binary.is_file():
            self.failures.append(f"missing_binary: {self.binary}")
        else:
            self.binary_sha256 = sha256_file(self.binary)
        self.corpus = collect_corpus(self.config.get("corpus", []))
        if not self.corpus:
            self.failures.append("missing_corpus: " + ", ".join(spec["path"] for spec in self.config.get("corpus", [])))
        flags = compile_flags(self.args.build_dir, self.config.get("source"))
        sanitizers = sanitizers_from(flags)
        missing = [name for name in self.args.require_sanitizer if name not in sanitizers]
        self.instrumentation = {"cmake_cache": read_cmake_cache(self.args.build_dir),
                                "source": self.config.get("source"),
                                "compile_flags": flags, "sanitizers": sanitizers,
                                "required_sanitizers": list(self.args.require_sanitizer),
                                "missing_required_sanitizers": missing}
        if missing:
            self.failures.append("instrumentation_missing: " + ",".join(missing)
                                 + ("" if flags is not None else " (no compile_commands.json entry)"))
        return not self.failures

    def harness_argv(self, *arguments: str) -> list[str]:
        return [*self.launcher, str(self.binary), *arguments]

    def corpus_arguments(self) -> list[str]:
        return [entry["absolute"] for entry in self.corpus]

    # -- running -----------------------------------------------------------------------------
    def run_batch(self, first: int, count: int, deadline: float | None) -> Outcome:
        self.processes += 1
        self.log.header(f"{utc_now()} --seeded {first} {count}")
        outcome = supervise(self.harness_argv("--seeded", str(first), str(count), *self.corpus_arguments()),
                            self.environment, startup_timeout=self.startup_timeout,
                            candidate_timeout=self.candidate_timeout, rss_limit_mb=self.rss_limit_mb,
                            stop_event=self.stop_event, deadline=deadline, log=self.log)
        self.peak_rss_kb = max(self.peak_rss_kb, outcome.peak_rss_kb)
        if outcome.start and "capability" in outcome.start:
            self.capabilities.add(outcome.start["capability"])
        return outcome

    def next_count(self, now: float, deadline: float) -> int:
        remaining_seeds = self.last_seed - self.next_seed + 1
        if self.rate is None:
            count = int(self.config.get("initial_count", 16))
        else:
            count = int(self.rate * min(self.chunk_seconds, max(1.0, deadline - now) * 1.25)) + 1
        return max(1, min(count, remaining_seeds, 10_000_000))

    def execute(self) -> None:
        self.directory.mkdir(parents=True, exist_ok=True)
        self.started_at = utc_now()
        began = time.monotonic()
        if not self.prepare():
            self.finish(began)
            return
        self.log = BoundedLog(self.directory / "stderr.log")
        deadline = began + self.budget
        self.status = "running"
        self.write_summary(began)
        try:
            while True:
                now = time.monotonic()
                if self.stop_event.is_set():
                    self.stop_reason = "interrupted"
                    break
                if now >= deadline:
                    self.stop_reason = "budget"
                    break
                if self.next_seed > self.last_seed:
                    self.stop_reason = "seed_range"
                    break
                if len({item["key"] for item in self.findings}) >= self.max_findings:
                    self.stop_reason = "max_findings"
                    break
                first = self.next_seed
                count = self.next_count(now, deadline)
                outcome = self.run_batch(first, count, deadline)
                if not self.account(first, count, outcome):
                    self.stop_reason = "failure"
                    break
                self.write_summary(began)
        except Exception as error:  # a driver defect must still leave a summary that fails
            self.failures.append(f"driver_error: {error!r}")
            self.stop_reason = "failure"
        finally:
            self.finish(began)

    def account(self, first: int, count: int, outcome: Outcome) -> bool:
        """Books one process; returns False when the target cannot continue."""
        kind = outcome.kind
        if outcome.start is not None:
            required = self.config.get("required_capability")
            capability = outcome.start.get("capability", "")
            if required and required not in capability:
                self.failures.append(f"capability_mismatch: required {required!r}, harness reported {capability!r}")
                return False
        if kind in ("ok", "exit"):
            attempts = int(outcome.result.get("attempts", -1))
            if attempts < 0 or attempts > count or (attempts != count and not outcome.result.get("stopped")):
                self.failures.append(f"accounting_mismatch: seeds {first}+{count}, harness reported {attempts}")
                return False
            self.attempts += attempts
            self.next_seed = first + attempts
            for key, value in outcome.result.get("counters", {}).items():
                self.counters[key] = self.counters.get(key, 0) + int(value)
            if attempts and outcome.elapsed > 0:
                self.rate = attempts / outcome.elapsed
            if kind == "exit":
                self.record_exit_failure(first, attempts, outcome)
            return True
        if kind == "capability":
            self.failures.append("capability_unavailable: " + self.last_line(outcome))
            return False
        if kind == "startup":
            self.failures.append(f"harness_error: exit {outcome.returncode}"
                                 f"{' (startup timeout)' if outcome.killed_for == 'startup' else ''}: "
                                 + self.last_line(outcome))
            return False
        if outcome.last_seed is None:
            # Killed (RSS) or stopped before the first candidate began: nothing to attribute.
            if kind == "stopped":
                return True  # the loop sees the deadline or the interrupt
            self.failures.append(f"harness_error: {kind} before the first candidate: " + self.last_line(outcome))
            return False
        begun = outcome.last_seed - first + 1
        # The last progress snapshot accounts for the candidates before it; only the rest are
        # attempts whose counters died with the process.
        snapshot = outcome.progress or {}
        accounted = min(int(snapshot.get("attempts", 0)), begun - 1)
        if accounted:
            for key, value in snapshot.get("counters", {}).items():
                self.counters[key] = self.counters.get(key, 0) + int(value)
        if kind == "stopped":
            # A harness that died of the stop signal itself: only completed candidates count.
            self.attempts += begun - 1
            self.unaccounted_attempts += begun - 1 - accounted
            self.next_seed = outcome.last_seed
            return True
        self.attempts += begun
        self.unaccounted_attempts += begun - accounted
        self.next_seed = outcome.last_seed + 1
        self.record_finding(kind, outcome.last_seed, first, outcome)
        return True

    @staticmethod
    def last_line(outcome: Outcome) -> str:
        lines = [line for line in outcome.stderr_tail.splitlines() if line.strip()]
        return lines[-1][:300] if lines else "(no stderr)"

    # -- findings ----------------------------------------------------------------------------
    def emit_input(self, seed: int) -> tuple[Path | None, dict]:
        self.findings_directory.mkdir(parents=True, exist_ok=True)
        staging = self.findings_directory / f".emit-{seed}"
        completed = subprocess.run(self.harness_argv("--emit", str(seed), str(staging), *self.corpus_arguments()),
                                   env=self.environment, capture_output=True, timeout=300)
        report = {}
        for line in completed.stdout.decode(errors="replace").splitlines():
            if line.startswith("cna_fuzz_emit "):
                report = json.loads(line[len("cna_fuzz_emit "):])
        if completed.returncode != 0 or not staging.is_file() or not report:
            if staging.exists():
                staging.unlink()
            return None, {"emit_error": f"exit {completed.returncode}: "
                                        + completed.stderr.decode(errors="replace")[-300:]}
        return staging, report

    def isolation_check(self, path: Path) -> dict:
        outcome = supervise(self.harness_argv("--replay-one", str(path)), self.environment,
                            startup_timeout=self.startup_timeout, candidate_timeout=self.candidate_timeout,
                            rss_limit_mb=self.rss_limit_mb)
        return {"reproduces_in_isolation": outcome.kind not in ("ok", "startup", "capability"),
                "isolation_kind": outcome.kind, "isolation_signature": outcome.signature}

    @staticmethod
    def finding_key(kind: str, outcome: Outcome) -> str:
        return f"{kind}|{outcome.sanitizer}|{outcome.dedup_signature or outcome.signal_name or outcome.returncode}"

    def repeated(self, kind: str, outcome: Outcome, seed) -> bool:
        """Counts a finding whose signature already has its kept inputs; True when it was one."""
        key = self.finding_key(kind, outcome)
        if sum(item["key"] == key for item in self.findings) < self.keep_per_signature:
            return False
        entry = self.repeats.setdefault(key, {"kind": kind, "count": 0, "seeds": []})
        entry["count"] += 1
        if len(entry["seeds"]) < 50:
            entry["seeds"].append(seed)
        return True

    def record_finding(self, kind: str, seed: int | None, first: int, outcome: Outcome,
                       seed_range: tuple[int, int] | None = None) -> None:
        """Keeps one finding: an exact seed with its input, a seed range, or (neither) a failure
        that a batch of zero candidates already shows, i.e. one that no input causes. Beyond
        ``keep_per_signature`` inputs per signature, a repeat is only counted, with its seed."""
        if self.repeated(kind, outcome, seed if seed is not None else list(seed_range) if seed_range else None):
            return
        record = {"schema_version": SCHEMA_VERSION, "target": self.name, "kind": kind,
                  "sanitizer": outcome.sanitizer, "signature": outcome.signature,
                  "seed": seed, "previous_seed_in_process": seed - 1 if seed is not None and seed > first else None,
                  "seed_range": list(seed_range) if seed_range else None,
                  "input_independent": seed is None and seed_range is None,
                  "seeded_replay": ({"first": seed_range[0], "count": seed_range[1] - seed_range[0] + 1}
                                    if seed_range else {"first": first, "count": 0} if seed is None else None),
                  "returncode": outcome.returncode, "signal": outcome.signal_name,
                  "rss_at_kill_mb": round(outcome.rss_at_kill_kb / 1024, 1) if outcome.rss_at_kill_kb else None,
                  "candidate_timeout_seconds": self.candidate_timeout, "rss_limit_mb": self.rss_limit_mb,
                  "binary": str(self.binary), "launcher": self.launcher, "binary_sha256": self.binary_sha256,
                  "corpus": [{key: entry[key] for key in ("path", "bytes", "sha256")} for entry in self.corpus],
                  "environment": self.recorded_environment, "found_at": utc_now(), "stderr_tail": outcome.stderr_tail[-16000:]}
        stem = (f"{kind}-seed-{seed}" if seed is not None else
                f"{kind}-seeds-{seed_range[0]}-{seed_range[1]}" if seed_range else f"{kind}-input-independent")
        if seed is not None:
            staging, emitted = self.emit_input(seed)
            record.update(emitted)
            if staging is not None:
                index = int(emitted.get("corpus_index", 0))
                suffix = Path(self.corpus[index]["path"]).suffix if index < len(self.corpus) else ""
                record["corpus_entry"] = self.corpus[index]["path"] if index < len(self.corpus) else None
                path = self.findings_directory / f"{stem}{suffix or '.bin'}"
                os.replace(staging, path)
                record.update({"input": path.name, "input_sha256": sha256_file(path),
                               "input_bytes": path.stat().st_size})
                if self.args.isolation_check:
                    record.update(self.isolation_check(path))
        record["reproduce"] = f"python3 tools/fuzz/reproduce.py {display_path(self.findings_directory / (stem + '.json'))}"
        write_json(self.findings_directory / f"{stem}.json", record)
        self.findings.append({key: record.get(key) for key in
                              ("kind", "seed", "seed_range", "input_independent", "sanitizer", "signature", "input",
                               "input_sha256", "reproduces_in_isolation")}
                             | {"key": self.finding_key(kind, outcome), "record": f"findings/{stem}.json"})

    def record_exit_failure(self, first: int, attempts: int, outcome: Outcome) -> None:
        """A failure after the result line (a leak report, say): bisect the batch to one seed.

        A control batch of zero candidates comes first: if it fails the same way, no input is the
        cause, and bisecting would only blame whichever seed happened to come first.
        """
        if any(item.get("input_independent") for item in self.findings):
            return  # already kept once; every later batch repeats it
        if self.repeated("exit", outcome, [first, first + max(1, attempts) - 1]):
            return
        control = self.run_batch(first, 0, None)
        self.bisection_runs += 1
        if control.kind == "exit":
            self.record_finding("exit", None, first, control)
            return
        low, count = first, max(1, attempts)
        failing = outcome  # the report of the smallest batch that still failed
        while count > 1 and self.bisection_runs < MAX_BISECTION_RUNS and not self.stop_event.is_set():
            half = count // 2
            probe = self.run_batch(low, half, None)
            self.bisection_runs += 1
            if probe.kind != "ok":
                count, failing = half, probe
                continue
            probe = self.run_batch(low + half, count - half, None)
            self.bisection_runs += 1
            if probe.kind != "ok":
                low, count, failing = low + half, count - half, probe
                continue
            break  # each half passes alone: the failure needs both, so keep the whole range
        if count == 1:
            self.record_finding("exit", low, low, failing)
        else:
            self.record_finding("exit", None, first, outcome, seed_range=(low, low + count - 1))

    # -- reporting ---------------------------------------------------------------------------
    def judge(self) -> None:
        interrupted = self.stop_reason == "interrupted"
        if not interrupted and not self.failures and not self.findings:
            if self.attempts == 0:
                self.failures.append("zero_attempts")
            elif self.counters.get("operations", 0) == 0:
                self.failures.append("zero_operations")
            for counter in self.config.get("required_counters", ()):
                if self.attempts and self.counters.get(counter, 0) == 0:
                    self.failures.append(f"required_counter_zero: {counter}")
        if self.findings:
            self.status = "findings"
        elif self.failures:
            self.status = "failed"
        elif interrupted:
            self.status = "interrupted" if self.processes else "not_run"
        else:
            self.status = "passed"

    def finish(self, began: float) -> None:
        self.elapsed = time.monotonic() - began
        self.finished_at = utc_now()
        self.judge()
        self.write_summary(began)
        if self.log:
            self.log.close()

    def summary(self) -> dict:
        return {
            "schema_version": SCHEMA_VERSION, "target": self.name, "status": self.status,
            "failures": self.failures, "stop_reason": self.stop_reason,
            "binary": str(self.binary), "binary_sha256": self.binary_sha256,
            "instrumentation": self.instrumentation,
            "environment": self.recorded_environment, "required_capability": self.config.get("required_capability"),
            "reported_capabilities": sorted(self.capabilities),
            "required_counters": self.config.get("required_counters", []),
            "corpus": {"files": len(self.corpus), "bytes": sum(entry["bytes"] for entry in self.corpus),
                       "entries": [{key: entry[key] for key in ("path", "bytes", "sha256")} for entry in self.corpus]},
            "seed_range": {"first": self.first_seed, "last_allowed": self.last_seed,
                           "next_unattempted": self.next_seed},
            "budget_seconds": round(self.budget, 1), "elapsed_seconds": round(self.elapsed, 1),
            "attempts": self.attempts, "unaccounted_attempts": self.unaccounted_attempts,
            "attempts_per_second": round(self.attempts / self.elapsed, 2) if self.elapsed > 0 else None,
            "operations": self.counters.get("operations", 0), "counters": self.counters,
            "harness_processes": self.processes, "bisection_runs": self.bisection_runs,
            "peak_rss_mb": round(self.peak_rss_kb / 1024, 1),
            "limits": {"candidate_timeout_seconds": self.candidate_timeout,
                       "startup_timeout_seconds": self.startup_timeout, "rss_limit_mb": self.rss_limit_mb,
                       "max_distinct_signatures": self.max_findings,
                       "kept_inputs_per_signature": self.keep_per_signature},
            **{label: sum(item["kind"] == kind for item in self.findings)
               + sum(entry["count"] for entry in self.repeats.values() if entry["kind"] == kind)
               for label, kind in (("crashes", "crash"), ("timeouts", "timeout"), ("rss_failures", "rss"),
                                   ("exit_failures", "exit"))},
            "distinct_signatures": len({item["key"] for item in self.findings}),
            "findings": self.findings,
            "repeated_findings": [{"key": key} | entry for key, entry in self.repeats.items()],
            "coverage": COVERAGE,
            "started_at": self.started_at, "finished_at": self.finished_at,
        }

    def write_summary(self, began: float) -> None:
        if self.status == "running":
            self.elapsed = time.monotonic() - began
        write_json(self.directory / "summary.json", self.summary())


# --------------------------------------------------------------------------------------------
# The campaign
# --------------------------------------------------------------------------------------------

def git_revision() -> dict:
    def ask(*arguments):
        try:
            return subprocess.run(["git", "-C", str(REPO_ROOT), *arguments], capture_output=True,
                                  text=True, timeout=30).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            return ""
    return {"head": ask("rev-parse", "HEAD") or None, "dirty": bool(ask("status", "--porcelain", "--untracked-files=no"))}


def plan_wall_seconds(budgets: list[float], jobs: int) -> float:
    """Longest-first schedule onto ``jobs`` workers, plus a fixed allowance per target."""
    workers = [0.0] * max(1, jobs)
    for budget in sorted(budgets, reverse=True):
        workers[workers.index(min(workers))] += budget + 120.0
    return max(workers) if budgets else 0.0


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path, help="build tree holding the harness binaries")
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--targets-file", type=Path, default=DEFAULT_TARGETS)
    parser.add_argument("--target", action="append", default=[], help="run only this target (repeatable)")
    parser.add_argument("--seed-start", type=lambda text: int(text, 0), default=1)
    parser.add_argument("--seed-count", type=lambda text: int(text, 0), default=None,
                        help="bound the seed range (default: open-ended)")
    parser.add_argument("--budget-scale", type=float, default=1.0)
    parser.add_argument("--budget-seconds", type=float, help="the same budget for every target")
    parser.add_argument("--jobs", type=int, default=1, help="targets run concurrently (default 1)")
    parser.add_argument("--require-sanitizer", action="append", default=[],
                        help="fail a target whose translation unit lacks -fsanitize=<name>")
    parser.add_argument("--max-findings-per-target", type=int, default=5,
                        help="stop a target after this many distinct failure signatures")
    parser.add_argument("--keep-per-signature", type=int, default=3,
                        help="inputs kept per signature; later repeats are counted with their seeds")
    parser.add_argument("--max-wall-seconds", type=float, help="refuse a plan longer than this")
    parser.add_argument("--no-isolation-check", dest="isolation_check", action="store_false")
    parser.add_argument("--plan", action="store_true", help="print the plan and exit")
    args = parser.parse_args(argv)
    if args.seed_start < 0 or args.seed_start > MAX_SEED:
        parser.error("--seed-start must be an unsigned 64-bit value")
    if args.seed_count is None:
        args.seed_count = MAX_SEED - args.seed_start + 1
    if args.seed_count <= 0:
        parser.error("--seed-count must be positive")
    if args.budget_scale <= 0 or (args.budget_seconds is not None and args.budget_seconds <= 0):
        parser.error("budgets must be positive")
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    args.build_dir = args.build_dir.resolve() if args.build_dir else None
    args.output_dir = args.output_dir.resolve()
    return args


def main(argv=None) -> int:
    args = parse_arguments(argv)
    configuration = json.loads(args.targets_file.read_text(encoding="utf-8"))
    targets = configuration["targets"]
    unknown = sorted(set(args.target) - {target["name"] for target in targets})
    if unknown:
        print(f"unknown target(s): {', '.join(unknown)}", file=sys.stderr)
        return EXIT_FAILED
    selected = [target for target in targets if not args.target or target["name"] in args.target]
    stop_event = threading.Event()
    runs = [TargetRun(target, args, stop_event) for target in selected]
    wall = plan_wall_seconds([run.budget for run in runs], args.jobs)
    for run in runs:
        print(f"plan: {run.name:<20} budget {run.budget:8.0f} s  binary {display_path(run.binary)}")
    print(f"plan: {len(runs)} target(s), {args.jobs} at a time, at most ~{wall / 60:.0f} min wall "
          f"including a 2-minute allowance per target; seeds from {args.seed_start}")
    if args.max_wall_seconds is not None and wall > args.max_wall_seconds:
        print(f"refused: planned {wall:.0f} s exceeds --max-wall-seconds {args.max_wall_seconds:.0f}", file=sys.stderr)
        return EXIT_FAILED
    if args.plan:
        return EXIT_PASSED

    received: list[str] = []

    def on_signal(number, _frame):
        received.append(signal.Signals(number).name)
        stop_event.set()

    previous = {number: signal.signal(number, on_signal) for number in (signal.SIGINT, signal.SIGTERM)}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    campaign = {"schema_version": SCHEMA_VERSION, "started_at": utc_now(), "revision": git_revision(),
                "host": {"platform": platform.platform(), "cpus": os.cpu_count(),
                         "load_average": [round(value, 2) for value in os.getloadavg()],
                         "python": platform.python_version()},
                "options": {"build_dir": str(args.build_dir) if args.build_dir else None,
                            "targets_file": display_path(args.targets_file), "seed_start": args.seed_start,
                            "seed_count": args.seed_count, "budget_scale": args.budget_scale,
                            "budget_seconds": args.budget_seconds, "jobs": args.jobs,
                            "require_sanitizer": args.require_sanitizer,
                            "max_findings_per_target": args.max_findings_per_target,
                            "keep_per_signature": args.keep_per_signature},
                "coverage": COVERAGE}
    try:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(run.execute) for run in sorted(runs, key=lambda run: -run.budget)]
            while not all(future.done() for future in futures):
                time.sleep(0.2)
            for future in futures:
                future.result()
    finally:
        for number, handler in previous.items():
            signal.signal(number, handler)
    for run in runs:
        if run.status == "not_run" and run.started_at is None:
            run.stop_reason = "interrupted"
            run.judge()
            run.directory.mkdir(parents=True, exist_ok=True)
            write_json(run.directory / "summary.json", run.summary())
    statuses = {run.name: run.status for run in runs}
    exit_code = (EXIT_FINDINGS if any(status == "findings" for status in statuses.values()) else
                 EXIT_FAILED if any(status == "failed" for status in statuses.values()) else EXIT_PASSED)
    campaign.update({"finished_at": utc_now(), "interrupted_by": received[0] if received else None,
                     "exit_code": exit_code,
                     "targets": [{key: run.summary()[key] for key in
                                  ("target", "status", "failures", "stop_reason", "budget_seconds",
                                   "elapsed_seconds", "attempts", "attempts_per_second", "operations",
                                   "peak_rss_mb", "crashes", "timeouts", "rss_failures", "exit_failures")}
                                 | {"summary": f"{run.name}/summary.json"} for run in runs]})
    write_json(args.output_dir / "campaign.json", campaign)
    print(f"{'target':<20} {'status':<12} {'attempts':>12} {'att/s':>9} {'operations':>14} "
          f"{'elapsed':>8} {'rss MB':>7} findings")
    for run in runs:
        summary = run.summary()
        print(f"{run.name:<20} {run.status:<12} {run.attempts:>12} {summary['attempts_per_second'] or 0:>9} "
              f"{summary['operations']:>14} {run.elapsed:>8.0f} {summary['peak_rss_mb']:>7} {len(run.findings)}")
        for failure in run.failures:
            print(f"  failure: {failure}")
        for finding in run.findings:
            print(f"  finding: {finding['kind']} seed {finding['seed']} -> {run.directory / finding['record']}")
    print(f"campaign: exit {exit_code}{' (interrupted by ' + received[0] + ')' if received else ''}; "
          f"summary {args.output_dir / 'campaign.json'}")
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
