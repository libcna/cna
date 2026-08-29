#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Cache a successful configure-time audit by the content of all declared inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


SOURCE_SUFFIXES = frozenset({".c", ".cc", ".cpp", ".h", ".hpp", ".inl", ".m", ".mm"})
CACHE_SCHEMA = 1


def input_files(paths: list[Path]) -> list[Path]:
    """Return the stable, de-duplicated set of audit inputs."""
    files: set[Path] = set()
    for path in paths:
        if path.is_file():
            files.add(path.resolve())
        elif path.is_dir():
            files.update(
                candidate.resolve()
                for candidate in path.rglob("*")
                if candidate.is_file() and candidate.suffix in SOURCE_SUFFIXES
            )
        else:
            raise FileNotFoundError(f"configure-audit input does not exist: {path}")
    return sorted(files, key=lambda item: item.as_posix())


def fingerprint(repo: Path, paths: list[Path], command: list[str], seed: str = "") -> str:
    """Hash paths, contents, the command, and this helper's implementation."""
    digest = hashlib.sha256()
    digest.update(
        f"schema={CACHE_SCHEMA}\0python={sys.version_info[:3]}\0seed={seed}\0".encode()
    )
    for argument in command:
        digest.update(argument.encode("utf-8", errors="surrogateescape"))
        digest.update(b"\0")

    helper = Path(__file__).resolve()
    all_files = input_files(paths + [helper])
    for path in all_files:
        try:
            label = path.relative_to(repo).as_posix()
        except ValueError:
            label = path.as_posix()
        digest.update(label.encode("utf-8", errors="surrogateescape"))
        digest.update(b"\0")
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def load_cache(path: Path, expected_fingerprint: str) -> dict[str, object] | None:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return None
    if payload.get("schema") != CACHE_SCHEMA or payload.get("fingerprint") != expected_fingerprint:
        return None
    if payload.get("returncode") != 0:
        return None
    return payload


def write_cache(path: Path, payload: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(payload, stream, ensure_ascii=False)
            stream.write("\n")
        os.replace(temporary_name, path)
    except BaseException:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise


def emit(name: str, state: str, stdout: str, stderr: str) -> None:
    print(f"configure audit '{name}': {state}")
    if stdout:
        print(stdout, end="" if stdout.endswith("\n") else "\n")
    if stderr:
        print(stderr, file=sys.stderr, end="" if stderr.endswith("\n") else "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--no-cache", action="store_true")
    parser.add_argument("--digest-only", action="store_true")
    parser.add_argument("--seed", default="")
    parser.add_argument("--input", action="append", type=Path, default=[])
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command and not args.digest_only:
        parser.error("an audit command must follow '--'")
    if not args.no_cache and args.cache is None and not args.digest_only:
        parser.error("--cache is required unless --no-cache is used")

    repo = args.repo.resolve()
    paths = [path if path.is_absolute() else repo / path for path in args.input]
    try:
        current_fingerprint = fingerprint(repo, paths, command, args.seed)
    except OSError as error:
        print(f"configure audit '{args.name}' could not fingerprint its inputs: {error}", file=sys.stderr)
        return 2

    if args.digest_only:
        print(current_fingerprint)
        return 0

    if not args.no_cache:
        cached = load_cache(args.cache, current_fingerprint)
        if cached is not None:
            emit(args.name, "cache hit", str(cached.get("stdout", "")), str(cached.get("stderr", "")))
            return 0

    try:
        completed = subprocess.run(
            command,
            cwd=repo,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except OSError as error:
        print(f"configure audit '{args.name}' could not run: {error}", file=sys.stderr)
        return 2
    state = "passed; cache refreshed" if not args.no_cache and completed.returncode == 0 else "executed"
    emit(args.name, state, completed.stdout, completed.stderr)

    if completed.returncode == 0 and not args.no_cache:
        write_cache(
            args.cache,
            {
                "schema": CACHE_SCHEMA,
                "fingerprint": current_fingerprint,
                "returncode": completed.returncode,
                "stdout": completed.stdout,
                "stderr": completed.stderr,
            },
        )
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())
