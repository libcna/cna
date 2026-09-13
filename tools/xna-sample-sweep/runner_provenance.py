#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-021: what actually produced each sample's reference.

A sample's `.contentproj` says what the game's own build did. What produced the `.xnb` files in the
artefact root is the sample's `scripts/XnaPipelineRunner.cs`, a C# program that drives the genuine
`BuildContent` task, and the two do not always agree: most of those runners hand-list their assets
with an importer, a processor and a name and **no `ProcessorParameters` at all**, so the reference
was built with the processor's own defaults even where the project declares otherwise.

Particles3D is the case that shows it. Its project asks for `GenerateMipmaps=True`,
`TextureFormat=DxtCompressed` and `PremultiplyAlpha=False`; its runner passes none of them, and
XNA's `smoke.xnb` is 256x256 `Color` with one mip. Built with the project's parameters CNA answers
a nine-mip DXT5 -- correctly, for the project -- and built with the defaults it is byte for byte
the reference.

So this reads each runner and answers, per sample, which of three things it is:

  * **project-faithful** -- it loads the `.contentproj` and copies every item's metadata, so the
    project is the description of the build;
  * **enumerated** -- it walks the source directory, so every asset took its route from its
    extension and no parameters were set;
  * **explicit** -- it hand-lists assets, so only the `ProcessorParameters_*` it sets itself apply.

Reads only; writes nothing outside the sweep workspace.
"""
from __future__ import annotations

import argparse
import json
import os
import re

# A runner is recognised by what it does -- constructing Microsoft's own `BuildContent` task --
# rather than by its file name, because the name is not a convention: three samples call theirs
# `Xna4ContentDiagnostic.cs`, `Xna4AssetProbe.cs` and `RobotGameXna4StockProbe.cs`, and one
# (`SAMPLE-003`, pruned) has no `scripts/` directory left at all and keeps its runner beside the
# output it wrote (`plans/plan_xna_sample_xnb_sweep.md` `XNASWEEP-133`). The names below are only
# the preference order when a sample has more than one.
SCRIPT_NAMES = ("XnaPipelineRunner.cs", "XnaPipelineRunnerWin7.cs", "XnaPipelineRunnerWin7Local.cs",
                "DiagPipelineRunner.cs", "Xna4DiagnosticPipeline.cs")

_BUILD_CONTENT = re.compile(r"\bnew\s+BuildContent\b")
# The `.contentproj` is read as XML, its `Compile` items are walked, and each item's child elements
# are copied on to the task item under their own names. All three are needed: a runner that reads
# the project for the asset list but writes its own metadata names does not carry the project's
# parameters, which is what `SAMPLE-146`'s does -- it strips the `ProcessorParameters_` prefix, so
# `BuildContent`, which reads only that prefix, saw none of them and built the defaults.
_READS_XML = re.compile(r"\bXmlDocument\b|\bXDocument\b")
_WALKS_COMPILE = re.compile(r"[\"':]Compile[\"']|Descendants\(\s*\w+\s*\+\s*\"Compile\"")
# `SetMetadata(x.LocalName, ...)` and `SetMetadata(x.Name.LocalName, ...)` are the same runner;
# requiring a single dotted step read SAMPLE-014's as hand-listed, which threw away the
# `PremultiplyAlpha False` its project sets on 60 of its textures and marked every one of them a
# difference (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-205`).
_COPIES_METADATA = re.compile(
    r"SetMetadata\(\s*[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*\.(?:LocalName|Name)\s*,")
_STRIPS_PARAMETER_PREFIX = re.compile(
    r"SetMetadata\(\s*\w+\.Substring\(\s*\"ProcessorParameters_\"\.Length")

_METADATA = re.compile(r'SetMetadata\(\s*"ProcessorParameters_([A-Za-z0-9_]+)"\s*,\s*"([^"]*)"\s*\)')
# The same call in either spelling: the parameter named by a literal, or built as
# `"ProcessorParameters_" + parameterName`, with the value a literal or an argument.
_METADATA_ANY = re.compile(
    r'SetMetadata\(\s*(?:"ProcessorParameters_(?P<literal>[A-Za-z0-9_]+)"'
    r'|"ProcessorParameters_"\s*\+\s*(?P<built>[A-Za-z_][A-Za-z0-9_]*(?:\s*\[[^\]]*\])?))'
    r'\s*,\s*(?P<value>[^;]*?)\s*\)')
_STRING = re.compile(r'"([^"\\]*(?:\\.[^"\\]*)*)"')
_SOURCE_LIKE = re.compile(r'^[^"]*\.[A-Za-z0-9]{1,10}$')


def classify(text):
    """Which of the three shapes a runner is, judged on what it does with the project."""
    if (_READS_XML.search(text) and _WALKS_COMPILE.search(text)
            and _COPIES_METADATA.search(text) and not _STRIPS_PARAMETER_PREFIX.search(text)):
        return "project-faithful"
    if "Directory.GetFiles" in text or "EnumerateFiles" in text:
        return "enumerated"
    return "explicit"


def _methods(text):
    """Every `{ ... }` method body, as (name, parameters, start, end, declarations) over @p text."""
    found = []
    for signature in re.finditer(
            r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(([^;{)]*)\)\s*\{', text):
        if signature.group(1) in ("if", "for", "foreach", "while", "switch", "catch", "using",
                                  "lock", "return", "new"):
            continue
        depth = 0
        index = signature.end() - 1
        while index < len(text):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        raw = [part.strip() for part in signature.group(2).split(",") if part.strip()]
        parameters = [part.split()[-1] for part in raw]
        found.append((signature.group(1), parameters, signature.end(), index, raw))
    return found


def _split_arguments(text):
    """Splits a call's argument list on its top-level commas."""
    parts, depth, quoted, current = [], 0, False, ""
    index = 0
    while index < len(text):
        character = text[index]
        if quoted:
            current += character
            if character == "\\":
                index += 1
                if index < len(text):
                    current += text[index]
            elif character == '"':
                quoted = False
        elif character == '"':
            quoted = True
            current += character
        elif character in "([{":
            depth += 1
            current += character
        elif character in ")]}":
            depth -= 1
            current += character
        elif character == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += character
        index += 1
    if current.strip():
        parts.append(current.strip())
    return parts


def _call_sites(text, name, definitions):
    """Every call of @p name that is not inside one of @p name's own definitions."""
    sites = []
    for call in re.finditer(r'\b%s\s*\(' % re.escape(name), text):
        if any(start <= call.start() < end for _, _, start, end, _ in definitions):
            continue
        depth, index = 0, call.end() - 1
        while index < len(text):
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        sites.append(_split_arguments(text[call.end():index]))
    return sites


def _normalize(value):
    """A C# path literal as the sweep spells one: its escaped separators become `/`."""
    return value.replace("\\\\", "/").replace("\\", "/")


def _literal(expression):
    """The string a literal argument holds, or None when the argument is not one."""
    match = re.fullmatch(r'\s*"([^"\\]*(?:\\.[^"\\]*)*)"\s*', expression)
    return match.group(1) if match else None


def _asset_of(arguments):
    """The source file a call's arguments name: its first literal that looks like a file name."""
    for argument in arguments:
        value = _literal(argument)
        if value is not None and _SOURCE_LIKE.match(value) and "\\n" not in value:
            return _normalize(value)
    return None


def overrides(text):
    """`ProcessorParameters_*` a runner sets, keyed by the source file it sets them on.

    Two shapes occur, and both have to be read or the reconstruction blames CNA for a parameter the
    genuine build was given and this one was not.

    The first sets metadata on an item a nearby literal already named -- `ground.SetMetadata(...)`
    a few lines under `Asset("ground.fbx", ...)` -- and the nearest preceding file-like literal in
    the same method is the asset.

    The second sets it inside a *helper*, where there is no literal to find: Audio3D's
    `Asset(file, name, importer, processor, bool generateMipmaps)` sets
    `ProcessorParameters_GenerateMipmaps` only when its flag argument is `true`, RimLighting's
    six-argument overload takes the parameter's name and value as arguments, and ShadowMapping's
    takes `params string[]` and walks it two at a time. Those are attributed through the helper's
    call sites, binding arguments to parameters by position, which is how the genuine build decided
    them too.
    """
    found = {}
    methods = _methods(text)
    for match in _METADATA_ANY.finditer(text):
        enclosing = None
        for method in methods:
            if method[2] <= match.start() < method[3]:
                if enclosing is None or method[2] > enclosing[2]:
                    enclosing = method
        body_start = enclosing[2] if enclosing else 0
        direct = None
        for literal in _STRING.finditer(text[body_start:match.start()]):
            value = literal.group(1)
            if _SOURCE_LIKE.match(value) and "\\n" not in value:
                direct = value
        literal_name = match.group("literal")
        name_expression = literal_name if literal_name else match.group("built")
        value_expression = match.group("value")
        if direct is not None:
            name = literal_name if literal_name else None
            value = _literal(value_expression)
            if name is None or value is None:
                continue
            found.setdefault(_normalize(direct), {})[name] = value
            continue
        if enclosing is None:
            continue
        # A helper: read it at each of its call sites instead.
        guard = _guard_of(text, body_start, match.start())
        definitions = [m for m in methods if m[0] == enclosing[0]]
        variadic = _variadic_of(enclosing, name_expression, value_expression)
        for arguments in _call_sites(text, enclosing[0], definitions):
            binding = dict(zip(enclosing[1], arguments))
            asset = _asset_of(arguments)
            if asset is None:
                continue
            if variadic is not None:
                # `params string[]` walked two at a time: the call's trailing arguments are the
                # parameter names and values, in pairs, exactly as the loop reads them.
                rest = arguments[len(enclosing[1]) - 1:]
                for index in range(0, len(rest) - 1, 2):
                    name = _literal(rest[index])
                    value = _literal(rest[index + 1])
                    if name is not None and value is not None:
                        found.setdefault(asset, {})[name] = value
                continue
            if guard is not None and binding.get(guard, "").strip().lower() != "true":
                continue
            name = literal_name if literal_name else _resolve(name_expression, binding)
            value = _resolve(value_expression, binding)
            if name is None or value is None:
                continue
            found.setdefault(asset, {})[name] = value
    return found


def _variadic_of(method, name_expression, value_expression):
    """The `params` array this metadata call indexes, or None when it names no array."""
    declarations = method[4]
    if not declarations or not declarations[-1].startswith("params "):
        return None
    array = declarations[-1].split()[-1]
    indexed = re.compile(r'\b%s\s*\[' % re.escape(array))
    if indexed.search(name_expression or "") or indexed.search(value_expression or ""):
        return array
    return None


def _guard_of(text, body_start, position):
    """The parameter an `if (parameter)` guards this metadata call with, or None."""
    body = text[body_start:position]
    match = None
    for candidate in re.finditer(r'if\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*$',
                                 body.rstrip(), re.M):
        match = candidate
    return match.group(1) if match else None


def _resolve(expression, binding):
    """A literal, or a parameter bound to a literal at the call site."""
    value = _literal(expression)
    if value is not None:
        return value
    name = expression.strip()
    if name in binding:
        return _literal(binding[name])
    return None


def _findRunner(directory):
    """The sample-relative path of the runner that drove `BuildContent`, or None.

    Every C# file under the sample is read, because the runner's *name* is not a convention and one
    sample keeps no `scripts/` directory at all. Where a sample has more than one copy -- most keep
    the same file in `scripts/` and beside the output it wrote -- the `scripts/` copy is preferred,
    then the known names, then the shortest path, so the answer does not depend on walk order.
    """
    found = []
    for where, subdirs, files in os.walk(directory):
        subdirs.sort()
        # A sample's own game source is large and holds no runner; the pipeline runners live near
        # the top of the tree. Four levels is enough for every one of them and keeps this cheap.
        if where[len(directory):].count(os.sep) >= 4:
            subdirs[:] = []
        for name in sorted(files):
            if not name.endswith(".cs"):
                continue
            path = os.path.join(where, name)
            try:
                with open(path, encoding="utf-8", errors="replace") as handle:
                    text = handle.read()
            except OSError:
                continue
            if _BUILD_CONTENT.search(text):
                found.append(os.path.relpath(path, directory))
    if not found:
        return None
    def rank(relative):
        parts = relative.split(os.sep)
        name = parts[-1]
        return (0 if parts[0] == "scripts" else 1,
                SCRIPT_NAMES.index(name) if name in SCRIPT_NAMES else len(SCRIPT_NAMES),
                len(parts), relative)
    return sorted(found, key=rank)[0]


def buildConfiguration(text):
    """The `BuildConfiguration` the runner hands `BuildContent`, when it names one literally.

    It is not decoration: `EffectProcessor.DebugMode` defaults to `Auto`, which follows the build
    configuration, and `Debug` means *debug information and optimizations disabled*. A content
    project's own `Configuration` defaults to `Debug` and a runner that calls the task directly
    never reads it -- 109 of these runners pass `Debug` and 12 pass `Release`, and for those 12 the
    reference's effect blobs are optimized while a build following the project would compile them
    unoptimized. SAMPLE-006's `desaturate.fx` is 516 bytes compiled with `/Qstrip_debug` and 1,188
    with `/Zi /Od`, and its reference is the first (plans/plan_xna_sample_xnb_sweep.md
    `XNASWEEP-200`).

    A runner that computes the configuration rather than writing it -- six do -- answers nothing,
    because what it computed is not in the source.
    """
    found = re.search(r'BuildConfiguration\s*=\s*"([^"]+)"', text)
    return found.group(1) if found else None


def sourceRoot(text, sample):
    """The directory the runner hands `BuildContent` as `RootDirectory`, when it is not the
    project's own.

    An `Include` is relative to that directory, so a runner that points it somewhere else is
    building *different files* under the same project. One does: SAMPLE-146's runner sets it to
    `xna4-diagnostic\\content-source`, a copy of the game's `Content` whose `SimpleScreen.fx`
    compiles its pixel shader `ps_2_0` where the shipped one says `ps_1_1`, which XNA 4.0 and the
    June 2010 `fxc` both refuse. Reading the project against the shipped tree makes that one asset
    a build failure and the reference unexplainable; reading it against the tree the runner named
    builds it (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-212`).

    The **whole** expression is resolved, not its first identifier. SAMPLE-003 writes
    `RootDirectory = root + @"\\TexturesAndColors\\Content"`, and reading that as `root` moved
    every one of its assets out from under the sources and made eight of them build failures --
    a regression this function caused and this rule fixes (`XNASWEEP-216`). A part that is neither
    a string literal nor a `const string` this file declares answers nothing at all, because a root
    guessed at is worse than the project's own.

    @param text The runner's source.
    @param sample The sample directory's name, which anchors the path.
    @return The root relative to the sample, or None when it is not literal or not under it.
    """
    named = re.search(r"RootDirectory\s*=\s*([^\n;]+?)\s*,\s*$", text, re.MULTILINE)
    if named is None:
        return None
    constants = dict(re.findall(
        r'\bconst\s+string\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]+);', text))
    whole = _literalOf(named.group(1), constants)
    if whole is None:
        return None
    marker = "\\" + sample + "\\"
    if marker not in whole:
        return None
    relative = whole.split(marker, 1)[1].replace("\\", "/").strip("/")
    return relative or None


def _literalOf(expression, constants, depth=0):
    """The string a `+` chain of literals and `const string` names spells, or None.

    @param expression The C# expression's text.
    @param constants Every `const string` the file declares, by name.
    @param depth Guards a constant that refers to itself.
    @return The whole literal, or None when any part of it is not one.
    """
    if depth > 8:
        return None
    whole = ""
    for part in [one.strip() for one in expression.split("+")]:
        quoted = re.match(r'^@?"((?:[^"\\]|\\.)*)"$', part)
        if quoted:
            whole += quoted.group(1) if part.startswith("@") else quoted.group(1).encode(
                "utf-8").decode("unicode_escape")
            continue
        if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", part) and part in constants:
            resolved = _literalOf(constants[part], constants, depth + 1)
            if resolved is None:
                return None
            whole += resolved
            continue
        return None
    return whole


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default="/rv/tmp/samples")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    document = {"root": os.path.abspath(args.root),
                "generator": "tools/xna-sample-sweep/runner_provenance.py",
                "samples": {}}
    for sample in sorted(os.listdir(args.root)):
        directory = os.path.join(args.root, sample)
        if not os.path.isdir(directory):
            continue
        chosen = _findRunner(directory)
        if chosen is None:
            continue
        with open(os.path.join(directory, chosen), encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        document["samples"][sample] = {
            "script": chosen.replace(os.sep, "/"),
            "kind": classify(text),
            "parameterOverrides": overrides(text),
            "buildConfiguration": buildConfiguration(text),
            "sourceRoot": sourceRoot(text, sample),
        }
    counts = {}
    for entry in document["samples"].values():
        counts[entry["kind"]] = counts.get(entry["kind"], 0) + 1
    document["counts"] = counts
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(document, handle, indent=1, sort_keys=True)
        handle.write("\n")
    print(json.dumps({"samples": len(document["samples"]), **counts}, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
