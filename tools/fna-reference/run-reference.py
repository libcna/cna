#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Build a pinned FNA checkout, generate two fresh dumps and compare at zero tolerance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PROJECT = Path(__file__).with_name("FnaReference.csproj")
COMPARATOR = ROOT / "scripts/compare-fna-reference.py"


def run(command, **kwargs):
    print("+ " + subprocess.list2cmdline([str(arg) for arg in command]), flush=True)
    return subprocess.run([str(arg) for arg in command], check=True, **kwargs)


def git(source, *arguments):
    return run(["git", "-C", source, *arguments], capture_output=True, text=True).stdout.strip()


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def output_paths(directory, mode, update):
    directory = directory.resolve()
    paths = [directory / f"{mode}-{index}.json" for index in (1, 2)]
    # Reject even new files inside the repository unless explicitly updating goldens.
    if not update and (directory == ROOT or ROOT in directory.parents):
        raise ValueError("Repository output requires --update; use an external output directory for fresh evidence")
    for path in paths:
        if path.is_symlink():
            raise ValueError(f"Refusing symlink output: {path}")
        if path.exists() and not update:
            raise ValueError(f"Output exists: {path}; choose a fresh directory or explicitly pass --update")
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fna-source", default=os.environ.get("FNA_SOURCE_DIR"))
    parser.add_argument("--expected-revision", default=os.environ.get("FNA_EXPECTED_REVISION"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--mode", choices=("values", "effects", "effect-states", "effect-pixels"), default="values")
    parser.add_argument("--effects-dir", type=Path)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--update", action="store_true", help="Explicitly permit repository output and replacing these two dumps")
    parser.add_argument("--compare-to", type=Path, help="Also compare a CNA candidate dump (default numeric tolerance)")
    args = parser.parse_args()
    if not args.fna_source or not args.expected_revision:
        parser.error("Set --fna-source/FNA_SOURCE_DIR and --expected-revision/FNA_EXPECTED_REVISION")
    if args.mode != "values" and (not args.effects_dir or not args.effects_dir.is_dir()):
        parser.error("Effect modes require --effects-dir")
    try:
        paths = output_paths(args.output_dir, args.mode, args.update)
        source = Path(args.fna_source).resolve()
        revision = git(source, "rev-parse", "HEAD")
        if revision != args.expected_revision:
            raise ValueError(f"Unexpected FNA source revision: {revision}")
        git(source, "diff", "--quiet", "--no-ext-diff", "--ignore-submodules=none", "HEAD", "--")
        project = source / "FNA.Core.csproj"
        env = dict(os.environ)
        run(["dotnet", "build", project, "--configuration", args.configuration,
             "--no-incremental", "-m:4", "-v:minimal", "-p:RestoreIgnoreFailedSources=true"], env=env)
        assembly = Path(run(["dotnet", "msbuild", project, "-getProperty:TargetPath",
                             "-p:Configuration=" + args.configuration],
                            env=env, capture_output=True, text=True).stdout.strip()).resolve()
        if git(source, "rev-parse", "HEAD") != revision:
            raise ValueError("FNA source revision changed during build")
        git(source, "diff", "--quiet", "--no-ext-diff", "--ignore-submodules=none", "HEAD", "--")
        env.update(FNA_SOURCE_DIR=str(source), FNA_EXPECTED_REVISION=revision,
                   FNA_ASSEMBLY_PATH=str(assembly), FNA_EXPECTED_SHA256=sha256(assembly))
        run(["dotnet", "build", PROJECT, "--configuration", args.configuration,
             "-m:4", "-v:minimal", "-p:FnaAssemblyPath=" + str(assembly)], env=env)
        harness = PROJECT.parent / "bin" / args.configuration / "net8.0" / "FnaReference.dll"
        paths[0].parent.mkdir(parents=True, exist_ok=True)
        for path in paths:
            command = ["dotnet", harness]
            if args.mode != "values":
                command.extend(["--" + args.mode, args.effects_dir.resolve()])
            command.append(path)
            run(command, env=env)
        data = json.loads(paths[0].read_text(encoding="utf-8"))
        categories = ["NonRenderingApis", "PackedVector", "Viewport", "Vector2Edges", "MatrixPerspectiveEdges", "Vector3Edges", "Vector4Edges", "QuaternionEdges", "MatrixEdges"] if args.mode == "values" else [
            key for key, value in data.items() if key != "metadata" and isinstance(value, (dict, list))]
        if not categories:
            raise ValueError("Probe emitted no comparable categories")
        for category in categories:
            run([sys.executable, COMPARATOR, *paths, "--tolerance", "0", "--category", category])
        if args.compare_to:
            command = [sys.executable, COMPARATOR, paths[0], args.compare_to]
            for category in categories:
                command.extend(["--category", category])
            run(command)
        print(f"PASS: {len(categories)} categories reproduced with identical reference identity at zero tolerance")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Reference recipe failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
