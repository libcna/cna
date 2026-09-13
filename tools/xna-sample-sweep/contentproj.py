#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""plans/plan_xna_sample_xnb_sweep.md XNASWEEP-010: read an XNA `.contentproj` the way MSBuild does.

A second reader of the same file `modules/content-pipeline/src/Xna/Tasks/ContentProject.cpp` reads,
written from the MSBuild schema rather than from that C++ -- so a mapping it produces is evidence
about the project rather than a restatement of CNA's opinion of it.

What it answers, per `Compile` item: the source path relative to the project, the logical asset name
XNA's own build produced for it (the item's directory plus its `Name`, `%(Filename)` when `Name` is
absent), the importer, the processor and every `ProcessorParameters_*` value.
"""
from __future__ import annotations

import os
import posixpath
import re
import xml.etree.ElementTree as ET


def _local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1] if "}" in tag else tag.rsplit(":", 1)[-1]


def _expand(text: str, properties):
    """`$(Name)` replaced by the last value read for it; an unset property expands to nothing."""
    def repl(match):
        name = match.group(1).lower()
        for key, value in reversed(properties):
            if key.lower() == name:
                return value
        return ""
    return re.sub(r"\$\(([^)]*)\)", repl, text)


_COMPARISON = re.compile(r"^\s*'([^']*)'\s*(==|!=)\s*'([^']*)'\s*$")


def _condition(condition: str, properties):
    """(decidable, take) for the one condition shape a content project uses."""
    if not condition or not condition.strip():
        return True, True
    match = _COMPARISON.match(condition)
    if not match:
        return False, False
    left = _expand(match.group(1), properties)
    right = _expand(match.group(3), properties)
    equal = left.lower() == right.lower()
    return True, equal if match.group(2) == "==" else not equal


class Item:
    __slots__ = ("kind", "include", "metadata")

    def __init__(self, kind, include, metadata):
        self.kind = kind
        self.include = include
        self.metadata = metadata

    def get(self, name, default=""):
        for key, value in self.metadata:
            if key.lower() == name.lower():
                return value
        return default

    @property
    def source(self):
        """The `Include` as a forward-slash relative path; every project spells it MSBuild's way."""
        return self.include.replace("\\", "/")

    @property
    def asset_name(self):
        """The logical content name XNA's build produced: the item's directory plus its `Name`."""
        directory = posixpath.dirname(self.source)
        name = self.get("Name") or posixpath.splitext(posixpath.basename(self.source))[0]
        name = name.replace("\\", "/")
        return posixpath.join(directory, name) if directory else name

    @property
    def processor_parameters(self):
        return {key[len("ProcessorParameters_"):]: value
                for key, value in self.metadata
                if key.lower().startswith("processorparameters_")}


class ContentProject:
    def __init__(self, path):
        self.path = os.path.abspath(path)
        self.directory = os.path.dirname(self.path)
        self.properties = []
        self.items = []
        self.undecidable = []
        self._read()

    def _read(self):
        with open(self.path, "rb") as handle:
            data = handle.read()
        if data[:3] == b"\xef\xbb\xbf":
            data = data[3:]
        root = ET.fromstring(data)
        for group in root:
            name = _local(group.tag).lower()
            if name not in ("propertygroup", "itemgroup"):
                continue
            decidable, take = _condition(group.get("Condition", ""), self.properties)
            if not decidable:
                self.undecidable.append(group.get("Condition", ""))
                continue
            if not take:
                continue
            for child in group:
                if not isinstance(child.tag, str):
                    continue
                decidable, take_child = _condition(child.get("Condition", ""), self.properties)
                if not decidable:
                    self.undecidable.append(child.get("Condition", ""))
                    continue
                if not take_child:
                    continue
                if name == "propertygroup":
                    self.properties.append((_local(child.tag), (child.text or "").strip()))
                    continue
                metadata = [(_local(m.tag), (m.text or "").strip()) for m in child
                            if isinstance(m.tag, str)]
                self.items.append(Item(_local(child.tag), (child.get("Include") or "").strip(),
                                       metadata))

    def msbuild_property(self, name, default=""):
        found = default
        for key, value in self.properties:
            if key.lower() == name.lower() and value:
                found = value
        return found

    @property
    def compile_items(self):
        return [i for i in self.items if i.kind.lower() == "compile"]

    @property
    def copied_items(self):
        return [i for i in self.items if i.kind.lower() in ("content", "none")]

    @property
    def references(self):
        return [i for i in self.items if i.kind.lower() in ("reference", "projectreference")]

    @property
    def custom_pipeline_references(self):
        """References that are not Microsoft's own pipeline assemblies -- a game's own component."""
        out = []
        for item in self.references:
            include = item.include
            if include.startswith("Microsoft.Xna.Framework"):
                continue
            out.append(include)
        return out
