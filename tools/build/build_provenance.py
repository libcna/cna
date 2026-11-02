#!/usr/bin/env python3
"""Capture effective CMake inputs without mistaking a requested profile for the built configuration."""

from __future__ import annotations

import hashlib
import os
import subprocess
from pathlib import Path
from typing import Any


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_cache(path: Path) -> dict[str, dict[str, str]]:
    entries: dict[str, dict[str, str]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")) or "=" not in line:
            continue
        declaration, value = line.split("=", 1)
        if ":" not in declaration:
            raise ValueError(f"invalid CMake cache declaration in {path}: {declaration!r}")
        name, kind = declaration.rsplit(":", 1)
        # CMake quotes keys containing ':' (for example a pkg-config result).
        entries[name.strip('"')] = {"type": kind, "value": value}
    return entries


def git_bytes(root: Path, *arguments: str) -> bytes:
    return subprocess.run(
        ["git", "-C", str(root), *arguments], check=True, capture_output=True
    ).stdout


def repository_identity(root: Path) -> dict[str, Any]:
    head = git_bytes(root, "rev-parse", "HEAD").decode().strip()
    status = git_bytes(root, "status", "--porcelain=v1", "--untracked-files=all")
    difference = git_bytes(root, "diff", "--no-ext-diff", "--no-textconv", "--binary", "HEAD", "--")
    untracked = git_bytes(root, "ls-files", "--others", "--exclude-standard", "-z")
    gitlinks: dict[str, str] = {}
    for record in git_bytes(root, "ls-tree", "-r", "-z", "HEAD").split(b"\0"):
        if record.startswith(b"160000 "):
            metadata, name = record.split(b"\t", 1)
            gitlinks[os.fsdecode(name)] = metadata.split()[2].decode("ascii")
    files: dict[str, Any] = {}
    for encoded in untracked.split(b"\0"):
        if not encoded:
            continue
        name = os.fsdecode(encoded)
        path = root / name
        files[name] = ({"symlink": os.readlink(path)} if path.is_symlink()
                       else {"sha256": file_sha256(path)})
    return {
        "path": str(root),
        "git_commit": head,
        "dirty": bool(status),
        "status_porcelain": status.decode("utf-8", errors="replace"),
        "tracked_diff_sha256": hashlib.sha256(difference).hexdigest(),
        "untracked_files": files,
        "recorded_gitlinks": gitlinks,
    }


def source_tree_identity(root: Path) -> dict[str, Any]:
    """Fingerprint source exports, which have no independent Git checkout metadata."""
    digest = hashlib.sha256()
    count = 0
    size = 0
    def fail_walk(error: OSError) -> None:
        raise error

    for current, directories, files in os.walk(root, onerror=fail_walk):
        directories[:] = sorted(name for name in directories if name != ".git")
        for name in list(directories):
            if (Path(current) / name).is_symlink():
                directories.remove(name)
                files.append(name)
        for name in sorted(files):
            path = Path(current) / name
            relative = os.fsencode(str(path.relative_to(root)))
            digest.update(len(relative).to_bytes(8, "little"))
            digest.update(relative)
            if path.is_symlink():
                payload = b"L" + os.fsencode(os.readlink(path))
            else:
                payload = b"F" + (path.stat().st_mode & 0o111).to_bytes(2, "little")
                payload += bytes.fromhex(file_sha256(path))
                size += path.stat().st_size
            digest.update(len(payload).to_bytes(8, "little"))
            digest.update(payload)
            count += 1
    return {"sha256": digest.hexdigest(), "files": count, "bytes": size}


def capture_provenance(build_dir: Path, source_dir: Path | None = None,
                       environment: dict[str, str] | None = None) -> dict[str, Any]:
    cache_path = build_dir / "CMakeCache.txt"
    entries = read_cache(cache_path)
    values = {name: entry["value"] for name, entry in entries.items()}
    environment = os.environ if environment is None else environment
    source_dir = (source_dir or Path(values["CMAKE_HOME_DIRECTORY"])).resolve()
    candidates = {source_dir}
    for name, value in values.items():
        if value and (name.endswith("_SOURCE_DIR") or name.startswith("FETCHCONTENT_SOURCE_DIR_")
                      or name in ("CNA_SHARP_RUNTIME_ROOT", "CNA_EASYGL_ROOT")):
            candidates.add(Path(value).resolve())

    repositories: dict[str, Any] = {}
    source_repository: str | None = None
    unresolved: list[str] = []
    while candidates:
        directory = candidates.pop()
        if not directory.is_dir():
            unresolved.append(str(directory))
            continue
        probe = subprocess.run(["git", "-C", str(directory), "rev-parse", "--show-toplevel"],
                               capture_output=True, text=True)
        if probe.returncode:
            # A source export need not be a checkout. Do not invent a dependency revision.
            unresolved.append(str(directory))
            continue
        root = Path(probe.stdout.strip()).resolve()
        if directory == source_dir:
            source_repository = str(root)
        if str(root) in repositories:
            continue
        repositories[str(root)] = repository_identity(root)
        modules = root / ".gitmodules"
        if modules.is_file():
            result = subprocess.run(
                ["git", "config", "--file", str(modules), "--get-regexp", r"^submodule\..*\.path$"],
                capture_output=True, text=True)
            if result.returncode not in (0, 1):
                raise RuntimeError(f"cannot read submodule paths from {modules}: {result.stderr.strip()}")
            for line in result.stdout.splitlines():
                child = (root / line.split(None, 1)[1]).resolve()
                if not (child / ".git").exists():
                    unresolved.append(str(child))
                else:
                    candidates.add(child)

    # Cache-resolved libraries, tools and toolchain files receive content identities too.
    # Include INTERNAL libraries (e.g. wgpu-native), not just public FILEPATH entries.
    file_inputs: dict[str, Any] = {}
    for name, entry in entries.items():
        if entry["type"] != "FILEPATH" and "LIBRARY" not in name and name != "CMAKE_TOOLCHAIN_FILE":
            continue
        for value in entry["value"].split(";"):
            path = Path(value)
            if not path.is_absolute() or not path.is_file():
                continue
            if path.resolve().is_relative_to(build_dir.resolve()):
                continue
            resolved = str(path.resolve())
            if resolved not in file_inputs:
                file_inputs[resolved] = {"bytes": path.stat().st_size, "sha256": file_sha256(path)}

    return {
        "source_dir": str(source_dir),
        "source_repository": source_repository,
        "cmake_cache": values,
        "cmake_cache_types": {name: entry["type"] for name, entry in entries.items()},
        "cmake_cache_sha256": file_sha256(cache_path),
        "repositories": dict(sorted(repositories.items())),
        "unversioned_source_dirs": sorted(set(unresolved)),
        "unversioned_source_trees": {name: source_tree_identity(Path(name))
                                     for name in sorted(set(unresolved)) if Path(name).is_dir()},
        "file_inputs": dict(sorted(file_inputs.items())),
        "environment": {name: environment[name] for name in (
            "CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "CMAKE_PREFIX_PATH",
            "CMAKE_TOOLCHAIN_FILE", "PKG_CONFIG_PATH", "PKG_CONFIG_LIBDIR", "CCACHE_BASEDIR",
            "CCACHE_DIR", "CCACHE_COMPILERCHECK", "CCACHE_SLOPPINESS") if name in environment},
        "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "capture_scope": "report-time inputs; hashes and dirty state are not a source/package archive",
    }
