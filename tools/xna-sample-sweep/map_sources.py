#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-011: map every genuine `.xnb` to the item that built it.

The corpus is output; the sample tree is input; nothing in either says which produced which. What
does say it is the pair of names: XNA writes an asset to `<output root>/<the item's directory>/<the
item's Name>.xnb`, so an item's asset name is a suffix of its output's path. This walks every
`.contentproj` of a sample, computes that name for every `Compile` item, and looks for reference
files whose path ends in it. The prefix that is left over is the output root the build used, and a
root is believed only because assets were found under it.

A sample's projects are then scored against each root, and the project that explains the most of a
root's files owns it. That is the build unit the sweep rebuilds: a project, a target read from the
reference header, and an output directory.

Writes a mapping JSON under the sweep workspace. Reads only; never writes to the corpus.
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import posixpath
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from contentproj import ContentProject  # noqa: E402

PLATFORM_NAMES = {"w": "Windows", "m": "WindowsPhone", "x": "Xbox360"}


def project_reference(path, root):
    """The project's path as the map records it: relative to the sample tree, or absolute.

    A synthesized project lives in the repository's own build directory rather than in the
    read-only sample tree, and a relative path out of one root and into another says nothing;
    `os.path.join` with an absolute second argument answers the absolute one, which is what
    `sweep.py` does with this (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-186`).
    """
    absolute = os.path.abspath(path)
    inside = os.path.abspath(root) + os.sep
    return os.path.relpath(absolute, root) if absolute.startswith(inside) else absolute


def ResolveIgnoringCase(path):
    """The file a project's `Include` names, resolved the way the reference build's NTFS did.

    An XNA content project spells its sources with whatever case its author typed, and the build
    that produced the reference read them on a filesystem that folds case: SAMPLE-138's project
    names `Textures\\backbreaking.png` and ships `Textures/Backbreaking.png`, SAMPLE-070's names
    `.JPG` and ships `.jpg`, SAMPLE-146's names `simplescreen.fx` and ships `SimpleScreen.fx`. The
    exact path wins whenever it exists; a component matching more than one entry is left alone,
    because two files differing only in case is a tree the reference build could not have had
    (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-163`). `cna-content` resolves the same way, so
    the mapper and the build agree about which file an item names.
    """
    if os.path.exists(path):
        return path
    resolved = os.sep if path.startswith(os.sep) else ""
    for part in path.strip(os.sep).split(os.sep):
        if not part or part == ".":
            continue
        if part == "..":
            resolved = os.path.join(resolved, part)
            continue
        candidate = os.path.join(resolved, part)
        if os.path.exists(candidate):
            resolved = candidate
            continue
        try:
            found = [name for name in os.listdir(resolved or ".") if name.lower() == part.lower()]
        except OSError:
            return path
        if len(found) != 1:
            return path
        resolved = os.path.join(resolved, found[0])
    return resolved


def sample_projects(root, sample, synthesized=None):
    """Every `.contentproj` of one sample, outside anybody else's output.

    A sample whose references were produced by a hand-written `BuildContent` runner ships no
    project at all, and `synthesize_projects.py` writes the one the runner describes beside a copy
    of its sources (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-186`).

    A reconstruction is **added** to whatever the sample ships rather than replacing it, because a
    sample can ship a project that is not the one its references came out of:
    `SAMPLES-DEC-007-Win7-SongProcessor` holds a hand-written `NinjAcademySongExport.contentproj`
    naming a source that tree has not got, while its own `.cmd` runner names five *other* samples'
    projects and copies one asset out of each. Adding rather than replacing leaves the ownership
    question to the root scoring below, which is where it belongs (`XNASWEEP-163`).
    """
    found = []
    base = os.path.join(root, sample)
    for directory, subdirs, files in os.walk(base):
        subdirs[:] = [d for d in sorted(subdirs)
                      if not d.startswith(("cna-", "fna-")) and d not in ("evidence", "obj", "bin")]
        for name in sorted(files):
            if name.lower().endswith(".contentproj"):
                found.append(os.path.join(directory, name))
    if not synthesized:
        return found
    staged = os.path.join(synthesized, sample)
    if not os.path.isdir(staged):
        return found
    for directory, subdirs, files in os.walk(staged):
        subdirs[:] = sorted(subdirs)
        for name in sorted(files):
            if name.lower().endswith(".contentproj"):
                found.append(os.path.abspath(os.path.join(directory, name)))
    return found


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--provenance", default=None,
                        help="runner-provenance.json; without it a project's parameters are used")
    parser.add_argument("--out", required=True)
    parser.add_argument("--synthesized", default=None,
                        help="directory of projects synthesize_projects.py wrote for samples "
                             "whose references came from a hand-written runner")
    args = parser.parse_args(argv)

    with open(args.manifest, encoding="utf-8") as handle:
        manifest = json.load(handle)
    provenance = {}
    if args.provenance and os.path.exists(args.provenance):
        with open(args.provenance, encoding="utf-8") as handle:
            provenance = json.load(handle).get("samples", {})
    root = manifest["root"]
    entries = manifest["entries"]

    by_sample = collections.defaultdict(list)
    for entry in entries:
        by_sample[entry["sample"]].append(entry)

    mappings, units, project_errors = [], [], []
    for sample in sorted(by_sample):
        references = by_sample[sample]
        paths = {e["relative"]: e for e in references}
        projects = {}
        for path in sample_projects(root, sample, args.synthesized):
            try:
                projects[path] = ContentProject(path)
            except Exception as error:  # noqa: BLE001 - a project that will not parse is a finding
                project_errors.append({"project": project_reference(path, root),
                                       "error": "%s: %s" % (type(error).__name__, error)})

        # An asset name is a path suffix of its output. Every (root, project, item) agreement.
        # root -> project -> {assetName: item}
        by_basename = collections.defaultdict(list)
        for relative in paths:
            by_basename[posixpath.basename(relative).lower()].append(relative)
        roots = collections.defaultdict(lambda: collections.defaultdict(dict))
        for project_path, project in projects.items():
            for item in project.compile_items:
                name = item.asset_name
                if not name:
                    continue
                suffix = "/" + name.lower() + ".xnb"
                for relative in by_basename.get(posixpath.basename(name).lower() + ".xnb", ()):
                    if relative.lower().endswith(suffix):
                        # Keyed without regard to case; see the lookup below for why.
                        found = relative[:len(relative) - len(suffix) + 1]
                        roots[found][project_path][name.lower()] = item

        # One project owns each root: the one that explains the most of its files, then the one
        # that is the sample's own rather than a diagnostic copy of it, then the one whose name
        # agrees with the target the reference headers carry. A sample with a Windows project and
        # an Xbox one names the same assets in both, so the count alone leaves the choice to sort
        # order and the wrong project's processor parameters get used.
        for output_root in sorted(roots):
            under = [r for r in paths if r.startswith(output_root)]
            headers = [paths[r] for r in under]
            platform_hint = collections.Counter(h.get("platform") for h in headers)
            wanted = platform_hint.most_common(1)[0][0] if platform_hint else None
            root_words = output_root.lower()

            # A sample that ships one project per exercise has several that explain a root
            # equally well, and the count alone leaves the choice to sort order: the
            # CatapultWars training kit's seven output roots all name 33 to 39 assets and four of
            # its projects declare the same ones, so three roots were read against another
            # exercise's `sky.png` and came out 800x720 against a reference of 800x480. The
            # variant's own name is what tells them apart, so it is scored: the words in the
            # output root's directory that also appear in the project's path.
            # Two components rather than one, because a runner that gives every asset an output
            # directory of its own puts the *asset* last and the thing that tells two assets of the
            # same name apart one above it. SAMPLE-141's five series each hold an `explosion`, and
            # the last component alone scores every one of them equally: the reference under
            # `3d-series-2-flightsim/explosion` was read against `2d-shooters/explosion.png` and
            # came out a 512x512 texture where the reference is a mono 8-bit sound
            # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-209`).
            components = output_root.rstrip("/").split("/")
            meaningful = [c for c in components if not c.lower().startswith("content")]
            variant_words = []
            for component in (meaningful[-2:] if len(meaningful) > 1 else meaningful[-1:]):
                spread = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ", component)
                variant_words += [w for w in re.split(r"[^A-Za-z0-9]+", spread.lower())
                                  if len(w) > 3 and w not in ("content", "build", "xna4", "bin")]

            def rank(project_path, explained):
                relative = os.path.relpath(project_path, root).replace(os.sep, "/").lower()
                pristine = 0 if ("diagnostic" in relative or "diag" in relative) else 1
                spaced = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", " ",
                                os.path.relpath(project_path, root)).lower()
                affinity_name = sum(1 for word in variant_words if word in spaced)
                # `m` is Windows Phone, `x` is Xbox 360, `w` is Windows.
                affinity = 0
                for token, letter in (("phone", "m"), ("xbox", "x"), ("windows", "w")):
                    in_project = token in relative
                    in_root = token in root_words
                    if in_project and wanted == letter:
                        affinity += 2
                    if in_project and in_root:
                        affinity += 1
                    if in_project and wanted is not None and wanted != letter:
                        affinity -= 1
                return (explained, pristine, affinity_name, affinity, -len(relative))

            owner, best_rank = None, None
            for project_path, items in sorted(roots[output_root].items()):
                candidate = rank(project_path, len(items))
                if best_rank is None or candidate > best_rank:
                    owner, best_rank = project_path, candidate
            best = best_rank[0]
            project = projects[owner]
            headers = [paths[r] for r in under]
            platforms = collections.Counter(h.get("platform") for h in headers)
            profiles = collections.Counter(bool(h.get("flags", 0) & 0x01) for h in headers)
            compressed = collections.Counter(bool(h.get("flags", 0) & 0x80) for h in headers)
            versions = collections.Counter(h.get("version") for h in headers)
            unit = {
                "sample": sample,
                "outputRoot": output_root.rstrip("/"),
                "project": project_reference(owner, root),
                "projectDirectory": project_reference(projects[owner].directory, root),
                "items": len(project.compile_items),
                "referencesUnderRoot": len(under),
                "explained": best,
                "platform": platforms.most_common(1)[0][0] if platforms else None,
                "platformMixed": len(platforms) > 1,
                "hiDef": profiles.most_common(1)[0][0] if profiles else None,
                "profileMixed": len(profiles) > 1,
                "compressed": compressed.most_common(1)[0][0] if compressed else None,
                "compressionMixed": len(compressed) > 1,
                "version": versions.most_common(1)[0][0] if versions else None,
                "customPipelineReferences": project.custom_pipeline_references,
                "undecidableConditions": project.undecidable,
            }
            units.append(unit)

            # One output root can be written by more than one `BuildContent` call, and one is:
            # SAMPLE-031's diagnostic runner writes two extra fonts into `bin-diag/Content` while
            # the sample's own project writes the rest of it. The root's owner is the project that
            # explains the most of it, and a *reconstruction* that names assets the owner does not
            # is a second call rather than a rival, so it gets a unit of its own
            # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-163`).
            for other_path, items in sorted(roots[output_root].items()):
                if other_path == owner or args.synthesized is None:
                    continue
                if not os.path.abspath(other_path).startswith(
                        os.path.abspath(args.synthesized) + os.sep):
                    continue
                extra = sorted(set(items) - set(roots[output_root][owner]))
                if not extra:
                    continue
                units.append(dict(unit, project=project_reference(other_path, root),
                                  items=len(items), explained=len(extra),
                                  secondPass=True,
                                  customPipelineReferences=projects[other_path]
                                      .custom_pipeline_references,
                                  undecidableConditions=projects[other_path].undecidable))

            owned = roots[output_root][owner]
            for relative in sorted(under):
                name = relative[len(output_root):-len(".xnb")]
                # Without regard to case, because the reference tree's own filesystem was.
                # Spacewar's project names `Textures\B1_nebula01.jpg`, so the item's asset name is
                # `Textures/B1_nebula01`; its `asteroid1.x` names `..\textures\asteroid1.tga`, so
                # the nested texture's is `textures/asteroid1_0`. On the Windows filesystem the
                # reference build ran on those are one directory, and the corpus has exactly one:
                # `bin/Content/textures/` holding both. Matching the item's authored case against
                # the reference's lost 115 references to `no-item` that the project does name
                # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-184`).
                item = owned.get(name.lower())
                itemProject = owner
                if item is None:
                    # Another project of the same sample may still name it -- and the source is
                    # then relative to *that* project's directory, not the owner's. Joining it to
                    # the winner's left SAMPLE-031's two diagnostic fonts `no-source`, because the
                    # root is won by the sample's own project and the fonts are named by the
                    # reconstruction of its diagnostic runner
                    # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-163`).
                    for other_path, items in roots[output_root].items():
                        if name.lower() in items:
                            item = items[name.lower()]
                            itemProject = other_path
                            break
                record = {
                    "reference": relative,
                    "sample": sample,
                    "outputRoot": output_root.rstrip("/"),
                    "assetName": name,
                    "sha256": paths[relative].get("sha256"),
                    "size": paths[relative].get("size"),
                    "platform": paths[relative].get("platform"),
                    "hiDef": bool(paths[relative].get("flags", 0) & 0x01),
                    "compressed": bool(paths[relative].get("flags", 0) & 0x80),
                    "version": paths[relative].get("version"),
                    "rootReader": paths[relative].get("rootReader"),
                }
                if item is None:
                    record["status"] = "no-item"
                    mappings.append(record)
                    continue
                source = ResolveIgnoringCase(
                    os.path.join(projects[itemProject].directory,
                                 item.source.replace("/", os.sep)))
                # A project's `ProcessorParameters` reached the reference only where the sample's
                # own runner passed them on. Most of these runners hand-list their assets and set
                # none, so the reference carries the processor's defaults whatever the project
                # says, and comparing against the project's parameters marks a correct build wrong
                # (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-021`).
                runner = provenance.get(sample, {})
                parameters = item.processor_parameters
                parameterSource = "project"
                if runner.get("kind") in ("explicit", "enumerated"):
                    overrides = {posixpath.basename(k): v
                                 for k, v in runner.get("parameterOverrides", {}).items()}
                    parameters = overrides.get(posixpath.basename(item.source), {})
                    parameterSource = "runner:" + runner["kind"]
                record.update({
                    # The project that *declares* the item, which is the owner except where a
                    # second `BuildContent` call named it (`XNASWEEP-163`).
                    "project": project_reference(itemProject, root),
                    "source": item.source,
                    "sourceRelative": os.path.relpath(source, root) if os.path.exists(source) else None,
                    "importer": item.get("Importer"),
                    "processor": item.get("Processor"),
                    "processorParameters": parameters,
                    "projectParameters": item.processor_parameters,
                    "parameterSource": parameterSource,
                    "status": "mapped" if os.path.isfile(source) else "no-source",
                })
                mappings.append(record)

        # Files under no believed root at all.
        explained = {m["reference"] for m in mappings}
        for relative in sorted(paths):
            if relative not in explained:
                mappings.append({
                    "reference": relative,
                    "sample": sample,
                    "assetName": None,
                    "sha256": paths[relative].get("sha256"),
                    "size": paths[relative].get("size"),
                    "platform": paths[relative].get("platform"),
                    "hiDef": bool(paths[relative].get("flags", 0) & 0x01),
                    "compressed": bool(paths[relative].get("flags", 0) & 0x80),
                    "version": paths[relative].get("version"),
                    "rootReader": paths[relative].get("rootReader"),
                    "status": "no-project" if not projects else "no-root",
                })

    mappings.sort(key=lambda m: m["reference"])
    units.sort(key=lambda u: u["outputRoot"])
    counts = collections.Counter(m["status"] for m in mappings)
    document = {
        "root": root,
        "generator": "tools/xna-sample-sweep/map_sources.py",
        "counts": {
            "references": len(mappings),
            "buildUnits": len(units),
            **{k: counts[k] for k in sorted(counts)},
        },
        "projectErrors": project_errors,
        "buildUnits": units,
        "mappings": mappings,
    }
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print(json.dumps(document["counts"], indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
