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
import re
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
    """The last rule whose pattern matches, or None.

    Without regard to case, because a runner classifies an asset by
    `Path.GetExtension(...).ToLowerInvariant()` and the filesystem it ran on folded case anyway:
    SAMPLE-141's flight-sim series holds `skybox_back.JPG`, which XNA built as a texture and a
    case-sensitive `fnmatch` against `*.jpg` does not match at all
    (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-194`).
    """
    found = None
    lowered = relative.lower()
    for rule in rules:
        pattern = rule["match"].lower()
        if fnmatch.fnmatch(lowered, pattern) or fnmatch.fnmatch(posixpath.basename(lowered),
                                                                pattern):
            found = rule
    return found


def everything(base, unit):
    """Every file under the unit's source root, matched or not, in a sorted walk's order."""
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
    return found


def sources(base, unit):
    """Every source file the unit's rules name, in the order a sorted walk gives them."""
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


def write(description, root, out, samples=None):
    """One description's projects, with the sources beside them."""
    written = []
    sample = description["sample"]
    for unit in description["units"]:
        staged = os.path.join(out, sample, unit["project"].rsplit(".", 1)[0])
        # A runner may build another sample's project: the Windows 7 `SongProcessor` export ran
        # five samples' own `.contentproj` files and copied one asset out of each, which is why
        # the reference sits in one sample's tree and its source in another's. `sourceSample`
        # says so (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-163`).
        base = os.path.join(
            os.path.join(samples, unit["sourceSample"]) if unit.get("sourceSample") and samples
            else root,
            unit["sourceRoot"].replace("/", os.sep))
        items = sources(base, unit)
        if os.path.isdir(staged):
            shutil.rmtree(staged)
        os.makedirs(staged)
        # Every file in the source root is copied, not only the ones that become items. A model
        # names its own textures and resolves them against the directory it sits in, and the
        # runner gave `BuildContent` that whole directory as its `RootDirectory`; staging only the
        # items leaves a `.x` pointing at a texture that is not there, which is how SAMPLE-141's
        # `skybox.x` failed with "primary source is not a regular file"
        # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-194`).
        for relative in everything(base, unit):
            target = os.path.join(staged, relative.replace("/", os.sep))
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(os.path.join(base, relative.replace("/", os.sep)), target)
        head = PROJECT_HEAD % (
            "tests/reference/xna40/build-units/%s.json" % sample,
            description["runner"], MSBUILD_NS, description["guid"],
            unit.get("platform", "Windows"), unit.get("profile", "Reach"),
            "true" if unit.get("compress") else "false")

        def entry(relative, rule):
            """One `<Compile>` element."""
            # `Minigun_S&WModel19.wav` is a real file in SoundLab: an unescaped `&` makes the
            # project unreadable, and the mapper then finds none at all.
            stem = posixpath.splitext(posixpath.basename(relative))[0]
            # A runner is free to give an asset a name that is not its file's stem. `assetName` is
            # that rule, with `{stem}` and `{relative}` for the two things a runner has to work
            # with; without it the name is the stem, which is what every runner here does
            # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-194`).
            name = rule.get("assetName", "{stem}").format(
                stem=stem, relative=posixpath.splitext(relative)[0])
            text = '    <Compile Include="%s">\n' % saxutils.escape(relative.replace("/", "\\"))
            text += "      <Name>%s</Name>\n" % saxutils.escape(name)
            text += "      <Importer>%s</Importer>\n" % rule["importer"]
            text += "      <Processor>%s</Processor>\n" % rule["processor"]
            for parameter in sorted(rule.get("parameters", {})):
                text += "      <ProcessorParameters_%s>%s</ProcessorParameters_%s>\n" % (
                    parameter, rule["parameters"][parameter], parameter)
            return text + "    </Compile>\n"

        # A runner that gives every asset an `OutputDirectory` of its own is not one project with
        # many items: it is one `BuildContent` call per asset, each with its own output root, and
        # the difference is visible in what a model's *nested* outputs are called. The genuine task
        # writes a nested texture at `OutputDirectory` + the texture source's own directory, and
        # the item's `Name` plays no part in it -- measured over thirteen cases of the genuine
        # `BuildContent` with `Name` and `Include` disagreeing about their directory
        # (`nested-names.json`, plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-196`). Folding the
        # per-asset output directory into the asset *name* puts the model in the right place and
        # its textures in the wrong one, which is what SAMPLE-141's six `SkyBox_*_0.xnb` were.
        # `outputDirectory` says the runner did it the other way, and the unit becomes one project
        # per asset, staged in the one directory they share.
        per_asset = any("outputDirectory" in rule for rule in unit["items"])
        if per_asset:
            for relative in items:
                rule = matches(unit["items"], relative)
                stem = posixpath.splitext(posixpath.basename(relative))[0]
                directory = rule.get("outputDirectory", "").format(
                    stem=stem, relative=posixpath.splitext(relative)[0])
                text = head + entry(relative, rule) + PROJECT_TAIL
                path = os.path.join(staged, "%s.contentproj" %
                                    re.sub(r"[^A-Za-z0-9._-]+", "_", directory or stem))
                with open(path, "w", encoding="utf-8", newline="\n") as handle:
                    handle.write(text)
                written.append((path, 1))
            continue

        text = head
        for relative in items:
            text += entry(relative, matches(unit["items"], relative))
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
        for path, count in write(description, sample, arguments.out, arguments.root):
            print("synthesize_projects: %s (%d item(s))"
                  % (os.path.relpath(path, REPO), count))
            total += count
    print("synthesize_projects: %d item(s) in total" % total)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
