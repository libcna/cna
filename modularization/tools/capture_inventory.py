#!/usr/bin/env python3
"""No-loss inventory capture for the CNA modularization campaign.

Captures machine-readable inventories of the repository (and optionally of a
configured build tree) so that pre/post-modularization states can be reconciled
item by item. Deterministic, sorted output; run it with the same arguments
before and after a restructuring phase and diff the results.

Usage:
  capture_inventory.py --out <dir> [--build <builddir> --config-name <name>]
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def sh(args, cwd=REPO):
    return subprocess.run(args, cwd=cwd, check=True, capture_output=True, text=True).stdout


def tracked(globs):
    out = subprocess.run(["git", "ls-files", "-z", "--"] + globs, cwd=REPO,
                         check=True, capture_output=True).stdout
    return sorted(p.decode("utf-8") for p in out.split(b"\0") if p)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def write_hashed(outfile, paths):
    with open(outfile, "w") as f:
        for p in paths:
            full = os.path.join(REPO, p)
            f.write(f"{sha256(full)}  {p}\n")


DECL_RE = re.compile(
    r"^\s*(?:template\s*<[^;{]*>\s*)?(class|struct|enum\s+class|enum)\s+"
    r"([A-Za-z_]\w*)\s*(?::|\{|;|$)", re.M)
COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def api_decls(headers):
    rows = []
    for p in headers:
        try:
            text = open(os.path.join(REPO, p), encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        text = COMMENT_RE.sub("", text)
        for m in DECL_RE.finditer(text):
            rows.append(f"{p}\t{m.group(1).replace(' ', '_')}\t{m.group(2)}")
    return sorted(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--build")
    ap.add_argument("--config-name", default="config")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    # Physical module layout: production files live under modules/**/{include,src},
    # module-owned tests under modules/**/tests, and the historical global trees (if any
    # remain -- pre-Phase-3 captures) keep their original meaning.
    module_files = tracked(["modules/"])
    production = (tracked(["include/", "src/"])
                  + [p for p in module_files
                     if re.search(r"/(include|src)/", p)])
    module_tests = [p for p in module_files if re.search(r"/tests/", p)]
    module_build = [p for p in module_files if p.endswith("CMakeLists.txt")
                    and not re.search(r"/(include|src|tests)/", p)]
    write_hashed(os.path.join(a.out, "files-production.tsv"), sorted(production))
    write_hashed(os.path.join(a.out, "files-tests.tsv"),
                 sorted(tracked(["tests/"]) + module_tests))
    write_hashed(os.path.join(a.out, "files-build-tooling.tsv"),
                 sorted(tracked(["CMakeLists.txt", "CMakePresets.json", "cmake/",
                                 "scripts/", "tools/", "examples/"]) + module_build))

    headers = [p for p in production if p.endswith((".hpp", ".h"))
               and "/include/" in ("/" + p)]
    with open(os.path.join(a.out, "api-decls.tsv"), "w") as f:
        f.write("\n".join(api_decls(headers)) + "\n")

    if a.build:
        ctest = subprocess.run(["ctest", "-N"], cwd=a.build,
                               capture_output=True, text=True).stdout
        names = sorted(re.findall(r"Test\s+#\d+:\s+(\S+)", ctest))
        with open(os.path.join(a.out, f"ctest-names-{a.config_name}.txt"), "w") as f:
            f.write("\n".join(names) + "\n")
        help_out = subprocess.run(
            ["cmake", "--build", a.build, "--target", "help"],
            capture_output=True, text=True).stdout
        targets = sorted(t[4:] for t in help_out.splitlines()
                         if t.startswith("... "))
        targets = [t.split(" ")[0] for t in targets]
        with open(os.path.join(a.out, f"targets-{a.config_name}.txt"), "w") as f:
            f.write("\n".join(targets) + "\n")

    print(f"inventory written to {a.out}")


if __name__ == "__main__":
    sys.exit(main())
