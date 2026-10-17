#!/usr/bin/env bash
# loc.sh - lines-of-code report for CNA: blank, comment and code lines per language and per
# category (library, build system, tests, examples, benchmarks, tools, scripts, CI, spikes,
# third-party patches, documentation, vendored third-party, submodules), per module, the
# generated files, and C++ on its own.
#
# The counting is tools/loc/loc_by_language.py with cloc as the engine; this wrapper prints its
# report as plain-text tables. The definitions (code/comment/blank line, source code, generated
# file, categories) are in the script's docstring.
#
# Usage: ./loc.sh [options]
#
#   ./loc.sh                      the whole report (submodules too when checked out)
#   ./loc.sh --no-submodules      first-party files and the vendored copies only; faster
#   ./loc.sh --rev <commit>       count a commit instead of the working tree
#   ./loc.sh --compare <base> --rev <commit> --per-commit
#                                 what changed between two commits, attributed per commit
#   ./loc.sh --format markdown --output <file>
#                                 the same report as Markdown
#
# Requires python3 and cloc (sudo apt-get install -y cloc). Always recomputes from git.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v cloc >/dev/null 2>&1; then
    echo "error: loc.sh needs cloc (https://github.com/AlDanial/cloc): sudo apt-get install -y cloc" >&2
    exit 1
fi

exec python3 "$ROOT_DIR/tools/loc/loc_by_language.py" --format text "$@"
