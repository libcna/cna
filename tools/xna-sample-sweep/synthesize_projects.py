#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-186: a content project for a sample that ships none.

`XNASWEEP-163` counted the references the sweep has no route to, and the largest class of them is
samples whose references were produced by a hand-written `BuildContent` runner rather than by a
`.contentproj`: `map_sources.py` finds no project, so no build unit exists and nothing is ever
compared. The remedy is the one that audit named -- read the runner's own asset list and write the
project it describes -- and this is it.

What each runner says is recorded once, by hand, in `tests/reference/xna40/build-units/*.json`,
because the runners are C# and reading them with a parser would be a second thing to get wrong.
Each description names the source root, the output root, the target, and the rule the runner's own
`Directory.GetFiles` loop follows; this tool turns one into a `.contentproj` beside a copy of the
sources, which is what `map_sources.py` and `sweep.py` already know how to build.

The sources are copied because the sample tree is read-only (§9) and a content build resolves an
item's `Include` against the project's own directory.

Usage:
    synthesize_projects.py [--descriptions <dir>] [--root <samples>] [--out <dir>]
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import os
import posixpath
import shutil
import sys
import xml.sax.saxutils as saxutils

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MSBUILD_NS = "http://schemas.microsoft.com/developer/msbuild/2003"

PROJECT_HEAD = """<?xml version="1.0" encoding="utf-8"?>
<!-- Written by tools/xna-sample-sweep/synthesize_projects.py from
     %s, which records what %s does.
     plans/plan_xna_sample_xnb_sweep.md XNASWEEP-186. -->
<Project ToolsVersion="4.0" DefaultTargets="Build" xmlns="%s">
  <PropertyGroup>
    <ProjectGuid>{%s}</ProjectGuid>
    <OutputType>Library</OutputType>
    <XnaPlatform>%s</XnaPlatform>
    <XnaProfile>%s</XnaProfile>
    <XnaCompressContent>%s</XnaCompressContent>
  </PropertyGroup>
  <ItemGroup>
"""

PROJECT_TAIL = """  </ItemGroup>
</Project>
"""


def matches(rules, relative):
    """The last rule whose pattern matches, or None."""
    found = None
    for rule in rules:
        pattern = rule["match"]
        if fnmatch.fnmatch(relative, pattern) or fnmatch.fnmatch(posixpath.basename(relative),
                                                                 pattern):
            found = rule
    return found


def sources(root, unit):
    """Every source file the unit's rules name, in the order a sorted walk gives them."""
    base = os.path.join(root, unit["sourceRoot"].replace("/", os.sep))
    found = []
    if unit.get("recurse", True):
        for directory, names, files in os.walk(base):
            names.sort()
            for name in sorted(files):
                found.append(os.path.relpath(os.path.join(directory, name), base)
                             .replace(os.sep, "/"))
    else:
        for name in sorted(os.listdir(base)):
            if os.path.isfile(os.path.join(base, name)):
                found.append(name)
    skip = set(unit.get("skip", []))
    return [one for one in found
            if posixpath.basename(one) not in skip and matches(unit["items"], one)]


def write(description, root, out):
    """One description's projects, with the sources beside them."""
    written = []
    sample = description["sample"]
    for unit in description["units"]:
        staged = os.path.join(out, sample, unit["project"].rsplit(".", 1)[0])
        base = os.path.join(root, unit["sourceRoot"].replace("/", os.sep))
        items = sources(root, unit)
        if os.path.isdir(staged):
            shutil.rmtree(staged)
        os.makedirs(staged)
        text = PROJECT_HEAD % (
            "tests/reference/xna40/build-units/%s.json" % sample,
            description["runner"], MSBUILD_NS, description["guid"],
            unit.get("platform", "Windows"), unit.get("profile", "Reach"),
            "true" if unit.get("compress") else "false")
        for relative in items:
            rule = matches(unit["items"], relative)
            source = os.path.join(base, relative.replace("/", os.sep))
            target = os.path.join(staged, relative.replace("/", os.sep))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(source, target)
            # `Minigun_S&WModel19.wav` is a real file in SoundLab: an unescaped `&` makes the
            # project unreadable, and the mapper then finds none at all.
            stem = posixpath.splitext(posixpath.basename(relative))[0]
            # A runner is free to give an asset a name that is not its file's stem, and one does:
            # SAMPLE-141's writes every asset into a directory of its own, so `background.jpg`
            # comes back as `background/background.xnb`. `assetName` is that rule, with `{stem}`
            # and `{relative}` for the two things a runner has to work with; without it the name
            # is the stem, which is what every other runner here does
            # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-194`).
            name = rule.get("assetName", "{stem}").format(
                stem=stem, relative=posixpath.splitext(relative)[0])
            text += '    <Compile Include="%s">\n' % saxutils.escape(relative.replace("/", "\\"))
            text += "      <Name>%s</Name>\n" % saxutils.escape(name)
            text += "      <Importer>%s</Importer>\n" % rule["importer"]
            text += "      <Processor>%s</Processor>\n" % rule["processor"]
            for name in sorted(rule.get("parameters", {})):
                text += "      <ProcessorParameters_%s>%s</ProcessorParameters_%s>\n" % (
                    name, rule["parameters"][name], name)
            text += "    </Compile>\n"
        text += PROJECT_TAIL
        path = os.path.join(staged, unit["project"])
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        written.append((path, len(items)))
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--descriptions",
                        default=os.path.join(REPO, "tests", "reference", "xna40", "build-units"))
    parser.add_argument("--root", default="/rv/tmp/samples")
    parser.add_argument("--out",
                        default=os.path.join(REPO, "build", "xna-sample-sweep", "synthesized"))
    arguments = parser.parse_args(argv)

    total = 0
    for name in sorted(os.listdir(arguments.descriptions)):
        if not name.endswith(".json"):
            continue
        with open(os.path.join(arguments.descriptions, name), encoding="utf-8") as handle:
            description = json.load(handle)
        sample = os.path.join(arguments.root, description["sample"])
        if not os.path.isdir(sample):
            print("synthesize_projects: %s is not in the tree; skipped" % description["sample"])
            continue
        for path, count in write(description, sample, arguments.out):
            print("synthesize_projects: %s (%d item(s))"
                  % (os.path.relpath(path, REPO), count))
            total += count
    print("synthesize_projects: %d item(s) in total" % total)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
