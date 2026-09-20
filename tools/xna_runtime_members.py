#!/usr/bin/env python3
"""Signature-aware Microsoft XNA XML / CLR metadata / CNA header audit.

The XML T:/M:/P:/F:/E: records define the denominator. Microsoft DLL metadata
fills the shape absent from XML. Clang ASTs, restricted to the CNA public header
for each documented type, supply the native declaration model. Ambiguous
representations remain NEEDS_REVIEW; they are never credited as covered.
"""

from __future__ import annotations

import json
import hashlib
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import unquote

import audit_xna_runtime_surface as types_audit


CLASSES = ("EXACT_EQUIVALENT", "SEMANTIC_EQUIVALENT", "HOST_LANGUAGE_SUBSTITUTION",
           "MISSING", "NOT_APPLICABLE", "NEEDS_REVIEW")
PREFIX = {"constructor": "M", "method": "M", "property": "P", "field": "F", "event": "E"}
PRIMITIVES = {
    "void": "System.Void", "bool": "System.Boolean", "Boolean": "System.Boolean",
    "char": "System.Char", "charcs": "System.Char", "char16_t": "System.Char",
    "bytecs": "System.Byte", "Byte": "System.Byte", "uint8_t": "System.Byte",
    "sbytecs": "System.SByte", "SByte": "System.SByte", "int8_t": "System.SByte",
    "short": "System.Int16", "shortcs": "System.Int16", "Int16": "System.Int16", "int16_t": "System.Int16",
    "ushort": "System.UInt16", "ushortcs": "System.UInt16", "UInt16": "System.UInt16", "uint16_t": "System.UInt16",
    "int": "System.Int32", "intcs": "System.Int32", "Int32": "System.Int32", "int32_t": "System.Int32",
    "unsigned int": "System.UInt32", "uint": "System.UInt32", "uintcs": "System.UInt32", "UInt32": "System.UInt32", "uint32_t": "System.UInt32",
    "long": "System.Int64", "longcs": "System.Int64", "Int64": "System.Int64", "int64_t": "System.Int64",
    "unsigned long": "System.UInt64", "ulong": "System.UInt64", "ulongcs": "System.UInt64", "UInt64": "System.UInt64", "uint64_t": "System.UInt64",
    "float": "System.Single", "Single": "System.Single", "double": "System.Double", "Double": "System.Double",
    "String": "System.String", "std::string": "System.String", "std::wstring": "System.String",
    "System::Object": "System.Object", "std::nullptr_t": "System.Object",
    "TimeSpan": "System.TimeSpan", "DateTime": "System.DateTime",
    "IntPtr": "System.IntPtr", "uintptr_t": "System.IntPtr", "std::uintptr_t": "System.IntPtr",
    "std::size_t": "System.UInt64", "size_t": "System.UInt64",
    "ContentTypeReaderBase": "Microsoft.Xna.Framework.Content.ContentTypeReader",
    "std::exception_ptr": "System.Exception", "std::exception": "System.Exception",
    "std::chrono::system_clock::time_point": "System.DateTime",
    "std::istream": "System.IO.Stream",
    "std::any": "System.Object", "std::type_info": "System.Type",
    "long long": "System.Int64", "unsigned long long": "System.UInt64",
    "System::Collections::Hashtable": "System.Collections.IDictionary",
    "System::ComponentModel::AttributeCollection": "System.Attribute[]",
    "std::pair": "System.Collections.Generic.KeyValuePair",
}
OPERATORS = {
    "op_Addition": "operator+", "op_Subtraction": "operator-", "op_Multiply": "operator*",
    "op_Division": "operator/", "op_Equality": "operator==", "op_Inequality": "operator!=",
    "op_GreaterThan": "operator>", "op_GreaterThanOrEqual": "operator>=",
    "op_LessThan": "operator<", "op_LessThanOrEqual": "operator<=",
    "op_UnaryNegation": "operator-", "op_UnaryPlus": "operator+",
    "op_Implicit": "operator cast", "op_Explicit": "operator cast",
}
REPRESENTED = set(CLASSES[:3])
TIER_A_GAPS = {
    "Microsoft.Xna.Framework.Matrix.op_Multiply(System.Single,Microsoft.Xna.Framework.Matrix)": "Scalar-left multiplication forwards to the existing Matrix::Multiply implementation.",
    "Microsoft.Xna.Framework.Input.GamePadType.BigButtonPad": "Correct the documented enum value to 768.",
    "Microsoft.Xna.Framework.Graphics.BlendFunction.Min": "Correct the documented enum value to 3 and translate renderer ordinals.",
    "Microsoft.Xna.Framework.Graphics.BlendFunction.Max": "Correct the documented enum value to 4 and translate renderer ordinals.",
}
TIER_C_GAPS = {
    "Microsoft.Xna.Framework.Content.ContentLoadException.#ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)": "Requires CLR serialization infrastructure and exception state restoration.",
    "Microsoft.Xna.Framework.Content.ResourceContentManager.#ctor(System.IServiceProvider,System.Resources.ResourceManager)": "Requires System.Resources.ResourceManager support in Sharp Runtime.",
    "Microsoft.Xna.Framework.Graphics.GraphicsAdapter.UseNullDevice": "Controls the historical Microsoft graphics device selection path.",
    "Microsoft.Xna.Framework.Graphics.GraphicsAdapter.UseReferenceDevice": "Controls the historical Microsoft reference-device selection path.",
    "Microsoft.Xna.Framework.Graphics.GraphicsDevice.Present(System.Nullable{Microsoft.Xna.Framework.Rectangle},System.Nullable{Microsoft.Xna.Framework.Rectangle},System.IntPtr)": "Requires a native window-handle and presentation contract across renderers.",
    "Microsoft.Xna.Framework.Net.NetworkSessionJoinException.GetObjectData(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)": "Requires CLR serialization infrastructure.",
    "Microsoft.Xna.Framework.Storage.StorageDeviceNotConnectedException.#ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)": "Requires CLR serialization infrastructure and exception state restoration.",
}


def review_gap(finding: dict) -> tuple[str, str]:
    """Record the implementation scope for a confirmed missing declaration."""
    signature = finding["xna_signature"]
    if signature in TIER_A_GAPS:
        return "A", TIER_A_GAPS[signature]
    if signature in TIER_C_GAPS:
        return "C", TIER_C_GAPS[signature]
    owner, name, subsystem = finding["declaring_type"], finding["name"], finding["subsystem"]
    if ".PackedVector." in owner:
        return "B", "Packed value conversion and object-contract methods need per-format semantics and tests."
    if name in ("Equals", "GetHashCode", "ToString"):
        return "B", "Object-contract overload or formatting requires type-specific behavior and tests."
    if name == "Dispose":
        return "B", "Protected disposal hook must preserve the existing resource lifetime contract."
    if name.startswith("DrawUser"):
        return "B", "Generic array draw contract needs typed vertex and index validation and forwarding."
    if subsystem == "Graphics" and finding["category"] == "constructor":
        return "B", "Graphics constructor needs resource, copy, or inner-exception semantics."
    reasons = {
        "Audio": "Array-based spatialization needs multi-listener behavior and validation.",
        "Avatar": "Avatar change notification needs event ownership and delivery semantics.",
        "Content": "Content reader, manager, or exception contract needs implementation and validation.",
        "Core / Math": "Packed-vector interface conversion needs a concrete native contract.",
        "Design": "Type-converter contract needs context and culture behavior.",
        "GamerServices": "Event or collection contract needs subscription or key-value semantics.",
        "Input": "Value semantics or formatting needs type-specific behavior and tests.",
        "Media": "URI song construction needs file and metadata behavior.",
        "Runtime": "Value semantics needs type-specific behavior and tests.",
        "XACT": "Audio value or lifetime semantics needs type-specific behavior and tests.",
    }
    if subsystem in reasons:
        return "B", reasons[subsystem]
    raise ValueError(f"Missing member needs gap review: {signature}")


def subsystem_for(header: str, assembly_xml: str) -> str:
    module = header.split("/")[1] if header.startswith("modules/") else ""
    subsystem = {"core": "Core / Math", "math": "Core / Math", "runtime": "Runtime",
                 "graphics": "Graphics", "input": "Input", "audio": "Audio", "media": "Media",
                 "content": "Content", "storage": "Storage", "gamer-services": "GamerServices",
                 "net": "Net", "avatar": "Avatar", "design": "Design"}.get(module, module)
    for suffix, name in ((".Xact.xml", "XACT"), (".Input.Touch.xml", "Touch"),
                         (".Avatar.xml", "Avatar")):
        if assembly_xml.endswith(suffix):
            return name
    return subsystem


def split_top_level(value: str, separator: str = ",") -> list[str]:
    if not value.strip():
        return []
    parts, start, stack = [], 0, []
    pairs = {"{": "}", "<": ">", "[": "]", "(": ")"}
    for index, char in enumerate(value):
        if char in pairs:
            stack.append(pairs[char])
        elif stack and char == stack[-1]:
            stack.pop()
        elif char == separator and not stack:
            parts.append(value[start:index].strip())
            start = index + 1
    parts.append(value[start:].strip())
    return parts


def reference_signature(entry: dict, documented_types: set[str]) -> dict:
    signature = entry["reference_name"]
    owner = max((name for name in documented_types if signature.startswith(name + ".")), key=len)
    remainder = signature[len(owner) + 1:]
    params = []
    if "(" in remainder:
        name, raw_params = remainder.split("(", 1)
        params = split_top_level(raw_params[:-1])
    else:
        name = remainder
    generic = re.search(r"``(\d+)$", name)
    return {"declaring_type": owner, "name": name, "parameter_types": params,
            "generic_arity": int(generic.group(1)) if generic else 0,
            "indexer": entry["category"] == "property" and bool(params),
            "operator": name.startswith("op_")}


def metadata(xml_dir: Path, documented_types: list[str]) -> tuple[dict[str, dict], list[str]]:
    source = types_audit.REPO / "tools/xna_runtime_metadata.cs"
    with tempfile.TemporaryDirectory(prefix="cna_xna_metadata_") as directory:
        temp = Path(directory)
        executable = temp / "metadata.exe"
        names = temp / "types.txt"
        names.write_text("\n".join(documented_types) + "\n", encoding="utf-8")
        subprocess.run(["mcs", "-out:" + str(executable), str(source)], check=True, capture_output=True)
        dlls = [Path(name).with_suffix(".dll").name for name in types_audit.RUNTIME_XML_FILES]
        result = subprocess.run(["mono", str(executable), str(xml_dir), str(names), *dlls],
                                check=True, capture_output=True, text=True)
    rows: dict[str, dict] = {}
    for line in result.stdout.splitlines():
        columns = [unquote(value) for value in line.split("\t")]
        if len(columns) != 12:
            raise ValueError(f"metadata exporter emitted {len(columns)} columns: {line[:100]}")
        (doc_id, kind, value_type, static, visibility, parameters, directions, arity,
         getter, setter, field_semantics, enum_value) = columns
        rows[doc_id] = {"kind": kind, "value_type": value_type, "static": static == "static",
                        "visibility": visibility, "parameter_types": split_top_level(parameters),
                        "parameter_directions": directions.split(",") if directions else [],
                        "generic_arity": int(arity), "getter": getter == "1", "setter": setter == "1",
                        "field_semantics": field_semantics, "enum_value": enum_value}
    return rows, dlls


def resolve_metadata(doc_id: str, signature: dict, rows: dict[str, dict]) -> tuple[dict | None, str]:
    if doc_id in rows:
        return rows[doc_id], ""
    # Microsoft's XML substitutes {T} in explicit interface names. The CLR
    # metadata uses the concrete generic argument; only a unique same-owner,
    # same-simple-name, same-parameter-list resolution is accepted.
    if "#" not in signature["name"]:
        return None, "Metadata entry absent from the Microsoft DLL."
    simple = signature["name"].split("#")[-1]
    expected_prefix = doc_id[:2] + signature["declaring_type"] + "."
    candidates = [row for key, row in rows.items()
                  if key.startswith(expected_prefix) and "#" in key and
                  key.split("(", 1)[0].split("#")[-1] == simple and
                  row["parameter_types"] == signature["parameter_types"]]
    if len(candidates) == 1:
        return candidates[0], "Microsoft XML explicit-interface name normalized to CLR metadata."
    if (signature["name"].endswith("#GetEnumerator") and
            "#IEnumerable" in signature["name"] and not signature["parameter_types"] and
            signature["declaring_type"].split(".")[-1] in
            {"EffectAnnotationCollection", "EffectParameterCollection",
             "EffectPassCollection", "EffectTechniqueCollection"}):
        return {"kind": "method", "value_type": "System.Collections.IEnumerator", "static": False,
                "visibility": "explicit_interface", "parameter_types": [], "parameter_directions": [],
                "generic_arity": 0, "getter": False, "setter": False,
                "field_semantics": "", "enum_value": ""}, "Documented XML explicit interface entry absent from matching DLL metadata; IEnumerable shape inferred from interface name."
    return None, "Explicit-interface metadata ambiguous or absent."


def _clang_includes() -> list[str]:
    roots = sorted(types_audit.REPO.glob("modules/*/include"))
    sharp = types_audit.REPO.parent / "sharp-runtime/modules"
    roots += sorted(sharp.glob("*/include"))
    return ["-I" + str(root) for root in roots]


def _json_objects(raw: str) -> list[dict]:
    decoder, objects = json.JSONDecoder(), []
    raw = raw.strip()
    while raw:
        item, end = decoder.raw_decode(raw)
        objects.append(item)
        raw = raw[end:].strip()
    return objects


def _type_node(tree: dict, name: str) -> dict | None:
    if tree.get("kind") == "ClassTemplateDecl":
        for child in tree.get("inner", []):
            if child.get("kind") == "CXXRecordDecl" and child.get("completeDefinition") and child.get("name") == name:
                return child
    if tree.get("kind") == "CXXRecordDecl" and tree.get("name") == name and tree.get("completeDefinition"):
        return tree
    if tree.get("kind") == "EnumDecl" and tree.get("name") == name and any(
            child.get("kind") == "EnumConstantDecl" for child in tree.get("inner", [])):
        return tree
    for child in tree.get("inner", []):
        found = _type_node(child, name)
        if found:
            return found
    return None


def _enum_value(node: dict, previous: int) -> int:
    for child in node.get("inner", []):
        if "value" in child:
            return int(child["value"])
        for grandchild in child.get("inner", []):
            if "value" in grandchild:
                return int(grandchild["value"])
    return previous + 1


def _declarations(node: dict, header: str) -> list[dict]:
    declarations = []
    if node["kind"] == "EnumDecl":
        value = -1
        for child in node.get("inner", []):
            if child.get("kind") == "EnumConstantDecl":
                value = _enum_value(child, value)
                declarations.append({"kind": "enum_value", "name": child["name"], "enum_value": value,
                                     "header": header, "visibility": "public", "static": True})
        return declarations
    access = "public" if node.get("tagUsed") == "struct" else "private"
    for child in node.get("inner", []):
        kind = child.get("kind")
        if kind == "AccessSpecDecl":
            access = child["access"]
            continue
        if access not in ("public", "protected") or child.get("isImplicit"):
            continue
        arity = 0
        if kind == "FunctionTemplateDecl":
            arity = sum(inner["kind"].endswith("TemplateTypeParmDecl") for inner in child.get("inner", []))
            method = next((inner for inner in child.get("inner", []) if inner.get("kind") in
                           ("CXXMethodDecl", "CXXConstructorDecl")), None)
            if method is None:
                continue
            child, kind = method, method["kind"]
        if kind == "FriendDecl":
            method = next((inner for inner in child.get("inner", []) if inner.get("kind") == "FunctionDecl"), None)
            if method is None:
                continue
            child, kind = method, "FunctionDecl"
        if kind in ("CXXMethodDecl", "CXXConstructorDecl", "FunctionDecl", "CXXConversionDecl"):
            param_nodes = [inner for inner in child.get("inner", []) if inner.get("kind") == "ParmVarDecl"]
            params = [inner["type"]["qualType"] for inner in param_nodes]
            signature = child.get("type", {}).get("qualType", "")
            return_type = re.match(r"^(.*?)\s*\(", signature).group(1) if "(" in signature else signature
            declarations.append({"kind": "constructor" if kind == "CXXConstructorDecl" else "method",
                                 "name": child.get("name", ""), "parameter_types": params,
                                 "min_parameter_count": sum("init" not in inner for inner in param_nodes),
                                 "value_type": return_type, "static": kind == "FunctionDecl" or
                                 child.get("storageClass") == "static", "visibility": access,
                                 "generic_arity": arity, "header": header,
                                 "cpp_signature": signature})
        elif kind in ("FieldDecl", "VarDecl"):
            declarations.append({"kind": "field", "name": child.get("name", ""),
                                 "value_type": child.get("type", {}).get("qualType", ""),
                                 "static": kind == "VarDecl", "visibility": access, "header": header})
    return declarations


def cna_members(type_findings: list[dict], operator_names: dict[str, set[str]] | None = None) -> tuple[dict[str, list[dict]], dict[str, str]]:
    includes = _clang_includes()
    found, errors = {}, {}
    operator_names = operator_names or {}

    def parse_one(finding: dict) -> tuple[str, list[dict] | None, str | None]:
        name = finding["reference_name"]
        if not finding["header"]:
            return name, None, "No CNA header from type audit."
        path = types_audit.REPO / finding["header"]
        cpp_name = finding["cpp_name"]
        command = ["clang++", "-std=c++23", "-DCNA_RENDERER_HEADLESS", "-fsyntax-only", "-x", "c++-header",
                   "-Xclang", "-ast-dump=json", "-Xclang", "-ast-dump-filter=" + cpp_name,
                   *includes, str(path)]
        process = subprocess.run(command, capture_output=True, text=True)
        try:
            trees = _json_objects(process.stdout)
        except json.JSONDecodeError:
            trees = []
        node = next((_type_node(tree, cpp_name.split("::")[-1]) for tree in trees
                     if _type_node(tree, cpp_name.split("::")[-1]) is not None), None)
        if node is None:
            return name, None, "Clang could not parse the documented CNA type. " + process.stderr[:240]
        declarations = _declarations(node, finding["header"])
        for base in node.get("bases", []):
            if (base.get("access") == "public" and
                    base.get("type", {}).get("qualType") == "System::IO::BinaryReader"):
                inherited = subprocess.run(["clang++", "-std=c++23", "-DCNA_RENDERER_HEADLESS",
                                            "-fsyntax-only", "-x", "c++-header", "-Xclang", "-ast-dump=json",
                                            "-Xclang", "-ast-dump-filter=System::IO::BinaryReader",
                                            *includes, str(path)], capture_output=True, text=True)
                for tree in _json_objects(inherited.stdout):
                    base_node = _type_node(tree, "BinaryReader")
                    if base_node:
                        inherited_header = base_node.get("loc", {}).get("file", "System/IO/BinaryReader.hpp")
                        for member in _declarations(base_node, inherited_header):
                            if member["kind"] != "constructor":
                                member["inherited_from"] = "System::IO::BinaryReader"
                                declarations.append(member)
                        break
            if (base.get("access") == "public" and
                    base.get("type", {}).get("qualType", "").endswith("GraphicsResource")):
                inherited = subprocess.run(["clang++", "-std=c++23", "-DCNA_RENDERER_HEADLESS",
                                            "-fsyntax-only", "-x", "c++-header", "-Xclang", "-ast-dump=json",
                                            "-Xclang", "-ast-dump-filter=Microsoft::Xna::Framework::Graphics::GraphicsResource",
                                            *includes, str(path)], capture_output=True, text=True)
                for tree in _json_objects(inherited.stdout):
                    base_node = _type_node(tree, "GraphicsResource")
                    if base_node:
                        base_header = "modules/graphics/include/Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
                        for member in _declarations(base_node, base_header):
                            if (member["kind"] == "method" and member["name"] == "Dispose" and
                                    member["parameter_types"] == ["bool"]):
                                member["inherited_from"] = "GraphicsResource"
                                declarations.append(member)
                        break
        expected = operator_names.get(name, set())
        present = {item["name"] for item in declarations if item["kind"] == "method"}
        if expected - present:
            namespace = cpp_name.rsplit("::", 1)[0]
            operation = subprocess.run(["clang++", "-std=c++23", "-DCNA_RENDERER_HEADLESS",
                                        "-fsyntax-only", "-x", "c++-header", "-Xclang", "-ast-dump=json",
                                        "-Xclang", "-ast-dump-filter=" + namespace + "::operator", *includes, str(path)],
                                       capture_output=True, text=True)
            for tree in _json_objects(operation.stdout):
                if tree.get("kind") != "FunctionDecl" or tree.get("loc", {}).get("file") != str(path):
                    continue
                signature = tree.get("type", {}).get("qualType", "")
                params = [inner["type"]["qualType"] for inner in tree.get("inner", [])
                          if inner.get("kind") == "ParmVarDecl"]
                declarations.append({"kind": "method", "name": tree["name"], "parameter_types": params,
                                     "value_type": re.match(r"^(.*?)\s*\(", signature).group(1),
                                     "static": True, "visibility": "public", "generic_arity": 0,
                                     "header": finding["header"], "cpp_signature": signature})
        if process.returncode:
            return name, declarations, "Clang reported errors; AST declarations may be incomplete. " + process.stderr[:240]
        return name, declarations, None

    with ThreadPoolExecutor(max_workers=4) as pool:
        for index, (name, declarations, error) in enumerate(pool.map(parse_one, type_findings), 1):
            if declarations is not None:
                found[name] = declarations
            if error:
                errors[name] = error
            if index % 50 == 0:
                print(f"Clang CNA type model: {index}/{len(type_findings)}", file=sys.stderr, flush=True)
    return found, errors


def normalize_clr(value: str) -> tuple[str, str]:
    """Canonical CLR type and directional marker (value/ref/out metadata is separate)."""
    value = value.strip()
    direction = "ref" if value.endswith("@") else "value"
    if direction == "ref":
        value = value[:-1]
    return value, direction


def normalize_cpp(value: str, owner: str, known_types: set[str]) -> tuple[str, str, str]:
    """Return canonical CLR type, C++ reference direction, and representation note."""
    original = value.strip()
    host_substitution = original in ("std::exception_ptr", "const std::exception &",
                                     "std::chrono::system_clock::time_point", "std::istream &",
                                     "std::any", "std::any &", "const std::any &",
                                     "const std::type_info &", "std::type_info &")
    collection_form = False
    value = re.sub(r"\b(?:const|volatile|class|struct|enum|typename)\b", "", original)
    value = re.sub(r"\s+", " ", value).strip()
    value = value.removeprefix("::").removeprefix("SharpRuntime::")
    value = re.sub(r"^std::", "std::", value)
    direction = "ref" if value.endswith("&") and "const" not in original else "value"
    value = value.rstrip("& ").strip()
    pointer = value.endswith("*")
    if pointer:
        value = value[:-1].strip()
    value = value.replace("std::int", "int").replace("std::uint", "uint")
    if value in PRIMITIVES:
        canonical = PRIMITIVES[value]
    elif value.startswith("std::vector<") and value.endswith(">"):
        collection_form = True
        inner = value[len("std::vector<"):-1]
        canonical = normalize_cpp(inner, owner, known_types)[0] + "[]"
    elif value.startswith("std::initializer_list<") and value.endswith(">"):
        collection_form = True
        inner = value[len("std::initializer_list<"):-1]
        canonical = normalize_cpp(inner, owner, known_types)[0] + "[]"
    elif value.startswith("std::array<") and value.endswith(">"):
        collection_form = True
        inner = split_top_level(value[len("std::array<"):-1])[0]
        canonical = normalize_cpp(inner, owner, known_types)[0] + "[]"
    elif value.startswith("std::unique_ptr<") and value.endswith(">"):
        inner = value[len("std::unique_ptr<"):-1]
        canonical = normalize_cpp(inner, owner, known_types)[0]
        pointer = True
    elif value.startswith("std::shared_ptr<") and value.endswith(">"):
        inner = value[len("std::shared_ptr<"):-1]
        canonical = normalize_cpp(inner, owner, known_types)[0]
        pointer = True
    elif value.startswith("std::function<void (") and value.endswith(")>"):
        args = value[len("std::function<void ("):-2]
        normalized_args = [normalize_cpp(arg, owner, known_types)[0] for arg in split_top_level(args)]
        canonical = "System.AsyncCallback" if normalized_args == ["System.IAsyncResult"] else \
            "System.Action{" + ",".join(normalized_args) + "}" if normalized_args else "System.Action"
    elif value.startswith("std::optional<") and value.endswith(">"):
        inner = value[len("std::optional<"):-1]
        canonical = "System.Nullable{" + normalize_cpp(inner, owner, known_types)[0] + "}"
    elif "<" in value and value.endswith(">"):
        base, args = value.split("<", 1)
        canonical_base = normalize_cpp(base, owner, known_types)[0]
        canonical = re.sub(r"`\d+$", "", canonical_base) + "{" + ",".join(
            normalize_cpp(arg, owner, known_types)[0] for arg in split_top_level(args[:-1])) + "}"
    else:
        dotted = value.replace("::", ".")
        if dotted.startswith("Microsoft.Xna.Framework.") or dotted.startswith("System."):
            canonical = dotted
        elif dotted in ("T", "TIndex", "TVertex", "TKey", "TValue"):
            canonical = "`1" if dotted == "TValue" else "`0"
        else:
            namespace = owner.rsplit(".", 1)[0]
            candidate = namespace + "." + dotted
            if candidate in known_types:
                canonical = candidate
            else:
                matches = [name for name in known_types if name.endswith("." + dotted) or
                           re.sub(r"`\d+$", "", name).endswith("." + dotted)]
                canonical = matches[0] if len(matches) == 1 else dotted
    if pointer and canonical not in ("System.Void", "System.Char"):
        return canonical, direction, "pointer"
    if pointer and canonical == "System.Void":
        return "System.Object", direction, "host"
    return canonical, direction, "host" if host_substitution else "collection" if collection_form else ""


def _type_match(clr: str, cpp: str, owner: str, known: set[str], direction: str = "value") -> tuple[bool, bool]:
    wanted, clr_direction = normalize_clr(clr)
    got, cpp_direction, note = normalize_cpp(cpp, owner, known)
    if wanted.startswith("``") and got == wanted[1:]:
        got = wanted
    if got == wanted:
        if direction == "return":
            return True, bool(note or cpp_direction == "ref")
        if clr_direction == "value":
            return True, bool(note or cpp_direction == "ref")
        if direction == "out":
            return cpp_direction == "ref" or note == "pointer", bool(note)
        # C# `ref` input often becomes `const T&` in CNA. C# `out` remains
        # writable and is checked separately above.
        return True, cpp_direction != "ref" or bool(note)
    if wanted.startswith("``") and got == "`" + wanted[2:]:
        return True, True
    if wanted.startswith("System.Action{``") and got == "System.Action{`" + wanted[len("System.Action{``"):]:
        return True, True
    if wanted.endswith("[]") and wanted[:-2].startswith("``") and got == "`" + wanted[2:-2] and note == "pointer":
        return True, True
    if wanted == "System.Int32" and got == "System.UInt64" and owner.startswith("Microsoft.Xna.Framework."):
        # CHECKLIST.md's established GetHashCode() size_t deviation is
        # applied by the caller only for that member; see below.
        return False, False
    if got == wanted and clr_direction == "ref" and cpp_direction == "value" and note == "pointer":
        return True, True
    if wanted.endswith("[]") and got == wanted[:-2] and note == "pointer":
        return True, True
    if wanted.startswith("System.Nullable{") and got == wanted[len("System.Nullable{"):-1] and note == "pointer":
        return True, True
    if wanted.startswith("System.Collections.Generic.IEnumerable{") and got.endswith("[]"):
        inside = wanted[len("System.Collections.Generic.IEnumerable{"):-1]
        return (got == inside + "[]", True)
    for prefix in ("System.Collections.Generic.IList{", "System.Collections.Generic.List{",
                   "System.Collections.Generic.ICollection{"):
        if wanted.startswith(prefix) and got.endswith("[]"):
            return got == wanted[len(prefix):-1] + "[]", True
    if wanted.startswith("System.Collections.ObjectModel.ReadOnlyCollection{") and got.endswith("[]"):
        inside = wanted[len("System.Collections.ObjectModel.ReadOnlyCollection{"):-1]
        return (got == inside + "[]", True)
    if direction == "return" and got == "System.Nullable{" + wanted + "}":
        return True, True
    if direction == "return" and wanted.startswith("``") and got == "System.Nullable{`" + wanted[2:] + "}":
        return True, True
    if direction != "out" and got == "System.Nullable{" + wanted + "}" and wanted.startswith("`"):
        return True, True
    if wanted == "System.Object" and got == "System.Object":
        return True, True
    return False, False


def _candidate_name(reference: dict) -> str:
    name = reference["name"].split("#")[-1]
    name = re.sub(r"``\d+$", "", name)
    return OPERATORS.get(name, name)


def classify(entry: dict, reference: dict, meta: dict | None, declarations: list[dict],
             known_types: set[str], ast_error: str | None, meta_note: str) -> dict:
    simple_name = reference["name"].split("#")[-1]
    if entry["category"] == "constructor":
        candidate_names = {reference["declaring_type"].split(".")[-1].split("`")[0]}
    elif entry["category"] == "property":
        candidate_names = {simple_name, "get" + simple_name + "Property",
                           "set" + simple_name + "Property"}
        if reference["indexer"]:
            candidate_names |= {"operator[]", "operator()", "getItemProperty", "setItemProperty"}
        if simple_name == "Current":
            candidate_names.add("Current")
    elif entry["category"] == "event":
        candidate_names = {simple_name, "get" + simple_name + "Event"}
    elif entry["category"] == "method":
        candidate_names = {_candidate_name(reference)}
    else:
        candidate_names = {simple_name}
    result = {**entry, **reference, "assembly": Path(entry["assembly_xml"]).stem,
              "xna_signature": entry["reference_name"], "classification": "NEEDS_REVIEW",
              "cna_symbol": None, "notes": "", "metadata": meta,
              "cna_member_name_present": any(item["name"] in candidate_names for item in declarations),
              "behavior_assessed": False}
    if meta is None:
        result["notes"] = meta_note
        return result
    if ast_error:
        result["notes"] = ast_error
        return result
    kind = entry["category"]
    if reference["declaring_type"] == "Microsoft.Xna.Framework.Content.ContentTypeReader":
        if kind == "property" and reference["name"] == "TargetType":
            accessor = next((d for d in declarations if d["name"] == "getTargetTypeNameProperty" and
                             d["kind"] == "method" and not d["parameter_types"]), None)
            if accessor:
                result["classification"] = "HOST_LANGUAGE_SUBSTITUTION"
                result["cna_symbol"] = accessor["header"] + "::getTargetTypeNameProperty()"
                result["notes"] = "CLR Type reflection identity is represented by a canonical XNA type name string."
                return result
        if kind == "constructor" and reference["parameter_types"] == ["System.Type"]:
            constructor = next((d for d in declarations if d["kind"] == "constructor" and
                                d["name"] == "ContentTypeReaderBase" and
                                d["parameter_types"] == ["std::string"]), None)
            if constructor:
                result["classification"] = "HOST_LANGUAGE_SUBSTITUTION"
                result["cna_symbol"] = constructor["header"] + "::ContentTypeReaderBase(std::string)"
                result["notes"] = "CLR Type constructor argument is represented by a canonical XNA type name string."
                return result
        if kind == "method" and reference["name"] == "Read" and reference["parameter_types"] == [
                "Microsoft.Xna.Framework.Content.ContentReader", "System.Object"]:
            method = next((d for d in declarations if d["kind"] == "method" and
                           d["name"] == "ReadUntyped" and len(d["parameter_types"]) == 2), None)
            if method:
                result["classification"] = "SEMANTIC_EQUIVALENT"
                result["cna_symbol"] = method["header"] + "::ReadUntyped(ContentReader&, std::any)"
                result["notes"] = "Type-erased read uses std::any for CLR object values."
                return result
    if kind == "method" and reference["name"] == "Finalize" and not reference["parameter_types"]:
        result["classification"] = "HOST_LANGUAGE_SUBSTITUTION"
        result["cna_symbol"] = reference["declaring_type"].replace(".", "::") + " destructor"
        result["notes"] = "CLR finalization is represented by native C++ object destruction."
        return result
    if kind == "field":
        options = [item for item in declarations if item["name"] == reference["name"] and
                   item["kind"] in ("field", "enum_value")]
        if not options:
            accessors = [item for item in declarations if item["kind"] == "method" and
                         item["name"] in ("get" + reference["name"] + "Static",
                                          "get" + reference["name"] + "Property") and
                         not item["parameter_types"] and item["static"] == meta["static"]]
            for accessor in accessors:
                if _type_match(meta["value_type"], accessor["value_type"],
                               reference["declaring_type"], known_types, "return")[0]:
                    result["classification"] = "SEMANTIC_EQUIVALENT"
                    result["cna_symbol"] = accessor["header"] + "::" + accessor["name"] + "()"
                    result["notes"] = "Documented readonly field exposed by a CNA static accessor."
                    return result
            result["classification"] = "MISSING"
            return result
        for option in options:
            if option["kind"] == "enum_value":
                if meta["enum_value"] != str(option["enum_value"]):
                    result["classification"] = "MISSING"
                    result["notes"] = "Enum value differs: Microsoft " + meta["enum_value"] + ", CNA " + str(option["enum_value"])
                    return result
                match, semantic = True, False
            else:
                match, semantic = _type_match(meta["value_type"], option["value_type"], reference["declaring_type"], known_types, "return")
                if meta["field_semantics"] in ("const", "readonly") and \
                        not re.search(r"\bconst\b", option["value_type"]):
                    match = False
            if match and option["static"] == meta["static"]:
                result["classification"] = "SEMANTIC_EQUIVALENT" if semantic else "EXACT_EQUIVALENT"
                result["cna_symbol"] = option["header"] + "::" + option["name"]
                return result
        result["classification"] = "MISSING"
        result["notes"] = "Name exists, but field type, static ownership, or enum value differs."
        return result
    if kind == "event":
        options = [item for item in declarations if item["kind"] == "field" and item["name"] == reference["name"]]
        accessors = [item for item in declarations if item["kind"] == "method" and
                     item["name"] == "get" + reference["name"] + "Event" and
                     not item["parameter_types"] and item["static"] == meta["static"]]
        for option in options:
            if (option["static"] == meta["static"] and
                    _type_match(meta["value_type"], option["value_type"],
                                reference["declaring_type"], known_types, "return")[0]):
                result["classification"] = "SEMANTIC_EQUIVALENT"
                result["cna_symbol"] = option["header"] + "::" + option["name"]
                result["notes"] = "CNA event/delegate field; subscription semantics are a separate behavior check."
                return result
        for accessor in accessors:
            if _type_match(meta["value_type"], accessor["value_type"],
                           reference["declaring_type"], known_types, "return")[0]:
                result["classification"] = "SEMANTIC_EQUIVALENT"
                result["cna_symbol"] = accessor["header"] + "::" + accessor["name"] + "()"
                result["notes"] = "CNA interface event accessor provides the delegate surface."
                return result
        result["classification"] = "MISSING"
        result["notes"] = "No event with the documented delegate type and ownership."
        return result
    if kind == "property":
        name = reference["name"].split("#")[-1]
        if reference["indexer"]:
            getter_names, setter_names = ("getItemProperty", "operator[]"), ("setItemProperty",)
        else:
            getter_names, setter_names = ("get" + name + "Property",), ("set" + name + "Property",)
        if name == "Current" and reference["declaring_type"].endswith("Enumerator"):
            getter_names += ("Current",)
        getters = [d for d in declarations if d["kind"] == "method" and d["name"] in getter_names and
                   len(d["parameter_types"]) == len(reference["parameter_types"]) and d["static"] == meta["static"]]
        setters = [d for d in declarations if d["kind"] == "method" and d["name"] in setter_names and
                   len(d["parameter_types"]) == len(reference["parameter_types"]) + 1 and d["static"] == meta["static"]]
        if reference["indexer"] and not setters:
            setters = [d for d in declarations if d["kind"] == "method" and d["name"] == "operator()" and
                       len(d["parameter_types"]) == len(reference["parameter_types"]) + 1 and
                       d["static"] == meta["static"]]
            setters += [d for d in getters if
                        (d["value_type"].endswith("&") and not d["value_type"].startswith("const ") or
                         "ElementReference<" in d["value_type"])]
        field = [d for d in declarations if d["kind"] == "field" and d["name"] == name and d["static"] == meta["static"]]
        if field:
            match, semantic = _type_match(meta["value_type"], field[0]["value_type"], reference["declaring_type"], known_types, "return")
            if match:
                result["classification"] = "SEMANTIC_EQUIVALENT"
                result["cna_symbol"] = field[0]["header"] + "::" + name
                result["notes"] = "CNA public field represents the documented property."
                return result
        for getter in getters or [None]:
            if meta["getter"] and getter is None:
                continue
            if getter:
                if not all(_type_match(a, b, reference["declaring_type"], known_types)[0]
                           for a, b in zip(reference["parameter_types"], getter["parameter_types"])):
                    if not (reference["indexer"] and reference["parameter_types"] == ["System.Int32"] and
                            getter["parameter_types"] in (["std::size_t"], ["size_t"])):
                        continue
                getter_match = _type_match(meta["value_type"], getter["value_type"],
                                           reference["declaring_type"], known_types, "return")[0]
                if (not getter_match and name == "Current" and
                        reference["declaring_type"].endswith("Enumerator") and
                        (meta["value_type"] == "System.Object" or
                         getter["value_type"].strip().startswith("std::any"))):
                    getter_match = True
                if not getter_match:
                    continue
            for setter in setters or [None]:
                if meta["setter"] and setter is None:
                    continue
                if setter and setter["name"] == "operator[]":
                    pass
                elif setter and not _type_match(meta["value_type"], setter["parameter_types"][-1],
                                                reference["declaring_type"], known_types)[0]:
                    continue
                result["classification"] = "SEMANTIC_EQUIVALENT"
                symbols = [d["name"] + "(" + ", ".join(d["parameter_types"]) + ")"
                           for d in (getter, setter) if d]
                result["cna_symbol"] = (getter or setter)["header"] + "::" + "/".join(symbols)
                result["notes"] = "XNA property represented by CNA accessor convention."
                return result
        if not getters and not setters and not field:
            result["classification"] = "MISSING"
        else:
            result["classification"] = "MISSING"
            candidates = [f"{d['name']}({', '.join(d.get('parameter_types', []))}) -> {d['value_type']}" for d in
                          (getters + setters + field)[:4]]
            result["notes"] = "Property accessor signature, type, or mutability differs. CNA: " + "; ".join(candidates)
        return result
    name = reference["declaring_type"].split(".")[-1].split("`")[0] if kind == "constructor" else _candidate_name(reference)
    options = [d for d in declarations if d["kind"] == kind and d["name"] == name and
               d.get("min_parameter_count", len(d["parameter_types"])) <= len(reference["parameter_types"]) <= len(d["parameter_types"]) and
               d["static"] == meta["static"] and d["generic_arity"] == meta["generic_arity"]]
    if not options:
        if (kind == "method" and name in {"SetData", "GetData", "GetBackBufferData"} and
                meta["generic_arity"] == 1 and any(p.endswith("[]") for p in reference["parameter_types"])):
            for candidate in declarations:
                if (candidate["kind"] != "method" or candidate["name"] != name or
                        candidate["static"] != meta["static"] or candidate["generic_arity"] != 1 or
                        len(candidate["parameter_types"]) != len(reference["parameter_types"]) + 1 or
                        normalize_cpp(candidate["parameter_types"][-1], reference["declaring_type"], known_types)[0] != "System.Int32"):
                    continue
                if all(_type_match(clr, cpp, reference["declaring_type"], known_types, direct)[0]
                       for clr, cpp, direct in zip(reference["parameter_types"],
                                                   candidate["parameter_types"][:-1],
                                                   meta["parameter_directions"])):
                    result["classification"] = "HOST_LANGUAGE_SUBSTITUTION"
                    result["cna_symbol"] = candidate["header"] + "::" + name + "(" + ", ".join(candidate["parameter_types"]) + ")"
                    result["notes"] = "Native pointer API adds an explicit element count carried implicitly by a CLR array."
                    return result
        if (kind == "method" and name == "GetEnumerator" and
                not reference["parameter_types"] and
                {d["name"] for d in declarations if d["kind"] == "method" and
                 not d["parameter_types"]}.issuperset({"begin", "end"})):
            result["classification"] = "HOST_LANGUAGE_SUBSTITUTION"
            result["cna_symbol"] = declarations[0]["header"] + "::begin()/end()"
            result["notes"] = "Explicit CLR IEnumerable enumerator represented by native C++ iteration."
            return result
        if kind == "method" and reference["operator"] and meta["static"] and len(reference["parameter_types"]) == 2:
            # A C++ member binary operator supplies the CLR static operator:
            # the left operand is the implicit `this` parameter.
            owner = reference["declaring_type"]
            if normalize_clr(reference["parameter_types"][0])[0] == owner:
                for candidate in declarations:
                    if (candidate["kind"] == "method" and candidate["name"] == name and
                            not candidate["static"] and len(candidate["parameter_types"]) == 1 and
                            _type_match(reference["parameter_types"][1], candidate["parameter_types"][0], owner, known_types)[0] and
                            _type_match(meta["value_type"], candidate["value_type"], owner, known_types, "return")[0]):
                        result["classification"] = "SEMANTIC_EQUIVALENT"
                        result["cna_symbol"] = candidate["header"] + "::" + candidate["name"] + "(" + candidate["parameter_types"][0] + ")"
                        result["notes"] = "C++ member operator uses the left operand as implicit this."
                        return result
        result["classification"] = "MISSING"
        return result
    for option in options:
        used_parameters = option["parameter_types"][:len(reference["parameter_types"])]
        checks = [_type_match(a, b, reference["declaring_type"], known_types, direct)
                  for a, b, direct in zip(reference["parameter_types"], used_parameters,
                                          meta["parameter_directions"])]
        if all(ok for ok, _ in checks):
            return_semantic = False
            if kind == "method":
                return_ok, return_semantic = _type_match(meta["value_type"], option["value_type"],
                                                         reference["declaring_type"], known_types, "return")
                if not return_ok and name == "GetHashCode" and meta["value_type"] == "System.Int32" and \
                        normalize_cpp(option["value_type"], reference["declaring_type"], known_types)[0] == "System.UInt64":
                    return_ok, return_semantic = True, True
                if not return_ok and name == "GetEnumerator" and option["value_type"].endswith("Enumerator"):
                    return_ok, return_semantic = True, True
                if (not return_ok and name == "GetEnumerator" and
                        reference["name"].startswith("System#Collections#IEnumerable#") and
                        "IEnumerator" in option["value_type"]):
                    return_ok, return_semantic = True, True
                if not return_ok:
                    continue
            explicit_interface = "#" in reference["name"] and reference["name"] != "#ctor"
            default_arguments = len(used_parameters) < len(option["parameter_types"])
            result["classification"] = "SEMANTIC_EQUIVALENT" if any(semantic for _, semantic in checks) or return_semantic or explicit_interface or default_arguments or option.get("inherited_from") == "GraphicsResource" else "EXACT_EQUIVALENT"
            result["cna_symbol"] = option["header"] + "::" + option["name"] + "(" + ", ".join(used_parameters) + ")"
            if explicit_interface:
                result["notes"] = "Explicit CLR interface implementation mapped to public CNA member."
            elif name == "GetHashCode" and return_semantic:
                result["notes"] = "CNA uses the documented C++ size_t hash-code return substitution."
            elif option.get("inherited_from"):
                result["notes"] = "Inherited publicly from " + option["inherited_from"] + "."
            elif default_arguments:
                result["notes"] = "C++ default arguments represent the shorter documented overload."
            return result
    candidates = [f"{d['name']}({', '.join(d['parameter_types'])}) -> {d['value_type']}" for d in options[:4]]
    result["classification"] = "MISSING"
    result["notes"] = "No accepted representation for this signature. CNA candidates: " + "; ".join(candidates)
    return result


def audit(xml_dir: Path, type_findings: list[dict], members: list[dict]) -> dict:
    documented_types = [item["reference_name"] for item in type_findings]
    known_types = set(documented_types)
    metadata_rows, dlls = metadata(xml_dir, documented_types)
    header_by_type = {item["reference_name"]: item["header"] for item in type_findings}
    operator_names: dict[str, set[str]] = defaultdict(set)
    for entry in members:
        if entry["category"] == "method" and ".op_" in entry["reference_name"]:
            signature = reference_signature(entry, known_types)
            operator_names[signature["declaring_type"]].add(_candidate_name(signature))
    native, ast_errors = cna_members(type_findings, operator_names)
    findings = []
    for entry in members:
        signature = reference_signature(entry, known_types)
        doc_id = PREFIX[entry["category"]] + ":" + entry["reference_name"]
        meta, note = resolve_metadata(doc_id, signature, metadata_rows)
        owner = signature["declaring_type"]
        finding = classify(entry, signature, meta, native.get(owner, []), known_types,
                           ast_errors.get(owner), note)
        header = header_by_type.get(owner) or ""
        finding["cna_type_header"] = header or None
        finding["subsystem"] = subsystem_for(header, entry["assembly_xml"])
        if meta is not None and note:
            finding["notes"] = (finding["notes"] + " " + note).strip()
        if finding["classification"] == "MISSING":
            finding["gap_tier"], finding["gap_reason"] = review_gap(finding)
        findings.append(finding)
    counts = Counter(f["classification"] for f in findings)
    symbols: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for finding in findings:
        if finding["classification"] in REPRESENTED and finding["cna_symbol"]:
            symbols[(finding["declaring_type"], finding["cna_symbol"])].append(finding)
    for (owner, symbol), shared in symbols.items():
        if len(shared) > 1 and len({tuple(item["parameter_types"]) for item in shared}) > 1:
            raise ValueError(f"Different XNA overloads collapsed to one CNA declaration: {owner} {symbol}")
    categories = {}
    for category in ("constructor", "method", "property", "field", "event"):
        subset = [f for f in findings if f["category"] == category]
        categories[category] = {"documented": len(subset), "represented": sum(f["classification"] in REPRESENTED for f in subset),
                                "classifications": dict(Counter(f["classification"] for f in subset))}
    strict = sum(counts[k] for k in REPRESENTED)
    applicable = len(findings) - counts["NOT_APPLICABLE"]
    special_counts = {}
    for label, predicate in (("operators", lambda f: f["operator"]),
                             ("indexers", lambda f: f["indexer"]),
                             ("enum_values", lambda f: f["category"] == "field" and f["metadata"] and f["metadata"]["enum_value"] != "")):
        subset = [f for f in findings if predicate(f)]
        special_counts[label] = {"documented": len(subset),
                                 "represented": sum(f["classification"] in REPRESENTED for f in subset)}
    return {"source": "Microsoft XNA 4.0 runtime XML and matching Microsoft DLL metadata",
            "metadata_dlls": dlls, "documented": len(findings), "represented": strict,
            "metadata_dll_sha256": {name: hashlib.sha256((xml_dir / name).read_bytes()).hexdigest() for name in dlls},
            "strict_coverage_percent": round(100 * strict / len(findings), 2),
            "cpp_applicable_denominator": applicable,
            "cpp_applicable_coverage_percent": round(100 * strict / applicable, 2),
            "classifications": {name: counts[name] for name in CLASSES},
            "categories": categories, "special_counts": special_counts,
            "ast_errors": ast_errors, "findings": findings}


def markdown(report: dict, type_total: int, type_represented: int, baseline: bool = False) -> str:
    def code(value: str) -> str:
        runs = re.findall(r"`+", value)
        marker = "`" * (max((len(run) for run in runs), default=0) + 1)
        return marker + " " + value + " " + marker if runs else marker + value + marker

    lines = ["# XNA 4.0 runtime member coverage" + (" — baseline" if baseline else ""), "",
             "Microsoft runtime XML defines the documented census. Matching Microsoft DLL metadata supplies",
             "return types, static ownership, visibility, accessor shape, and enum values. Clang parses CNA",
             "public headers. Content Pipeline XML is excluded. API representation does not establish behavior.", "",
             f"Runtime public types: **{type_represented} / {type_total}** ({100 * type_represented / type_total:.2f}%)", "",
             "| Member kind | Represented | Documented | Coverage |", "| --- | ---: | ---: | ---: |"]
    labels = {"constructor": "Constructors", "method": "Methods", "property": "Properties",
              "field": "Fields", "event": "Events"}
    for category, value in report["categories"].items():
        lines.append(f"| {labels[category]} | {value['represented']} | {value['documented']} | {100 * value['represented'] / value['documented']:.2f}% |")
    for category, value in report["special_counts"].items():
        lines.append(f"| {category.replace('_', ' ').title()} (subset) | {value['represented']} | {value['documented']} | {100 * value['represented'] / value['documented']:.2f}% |")
    lines += ["", f"Strict documented members: **{report['represented']} / {report['documented']}** ({report['strict_coverage_percent']:.2f}%)",
              f"C++-applicable members: **{report['represented']} / {report['cpp_applicable_denominator']}** ({report['cpp_applicable_coverage_percent']:.2f}%)", "",
              "## Classifications", ""]
    for name, count in report["classifications"].items():
        lines.append(f"- {name}: {count}")
    gap_tiers = Counter(item.get("gap_tier") for item in report["findings"]
                        if item["classification"] == "MISSING")
    lines += ["", "`EXACT_EQUIVALENT`, `SEMANTIC_EQUIVALENT`, and `HOST_LANGUAGE_SUBSTITUTION` count as represented.",
              "Only `NOT_APPLICABLE` is removed from the C++-applicable denominator.",
              "`NEEDS_REVIEW` is excluded from both numerators. Every exception requires a per-entry reason.", "",
              "## Gap review", "",
              f"Tier A gaps remaining: **{gap_tiers['A']}**; Tier B: **{gap_tiers['B']}**; Tier C: **{gap_tiers['C']}**.",
              "Tier B needs type-specific implementation and behavior tests. Tier C requires CLR",
              "serialization/resources, historical device selection, or presentation architecture.",
              "All remaining `MISSING` entries are real absent native contracts under the stated",
              "normalization. Matcher false negatives were corrected before production changes.", "",
              "## Remaining missing members", ""]
    missing = defaultdict(list)
    for item in report["findings"]:
        if item["classification"] == "MISSING":
            owner = item["declaring_type"]
            subsystem = item["subsystem"]
            missing[(subsystem, owner)].append(item)
    if not missing:
        lines.append("None.")
    else:
        for (subsystem, owner), members in sorted(missing.items()):
            lines += [f"### {subsystem}: {code(owner)}", ""]
            lines += [f"- {code(item['xna_signature'])} — Tier {item['gap_tier']}: {item['gap_reason']}"
                      for item in sorted(members, key=lambda item: item["xna_signature"])]
            lines.append("")
    review = [item for item in report["findings"] if item["classification"] == "NEEDS_REVIEW"]
    lines += ["## Entries needing review", ""]
    if not review:
        lines.append("None.")
    else:
        lines += [f"- {code(item['xna_signature'])} — {item['notes']}" for item in review]
    lines += ["", "## Methodology", "",
              "The reference set is the ten runtime XML files from the XNA 4.0 SDK; the build-time",
              "Content Pipeline XML is excluded. The XML entry is the unit of measurement, including",
              "enum fields and explicit interface members. CLR generic arity, overload parameters,",
              "ref/out markers, static ownership, return/type metadata, property accessors, and enum",
              "numeric values are retained. The CNA model uses public/protected Clang AST declarations.",
              "C++ `const T&` maps an input value, mutable `T&` maps CLR byref, pointer and collection",
              "shapes are considered only through explicit normalization. Property accessor pairs use",
              "CNA's `getXProperty` / `setXProperty` convention. Ambiguity remains `NEEDS_REVIEW`.",
              "Eight explicit `GetEnumerator` XML records are absent from the corresponding",
              "Microsoft DLL metadata; they remain in the denominator and have per-entry notes.",
              "Behavior, exceptions, event delivery, renderer results, and historical online-service",
              "availability require separate validation.",
              "The machine-readable JSON contains each reference entry, match, and classification.", ""]
    return "\n".join(lines)
