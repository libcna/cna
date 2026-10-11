#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Generate the mechanically regular C API routes from modules/c-api/spec/*.toml.

Most C routes are the same few lines around one line of real work: refuse a null output, look the
handle up, convert, call, convert back. The spec states only that one line; this tool writes the
rest. Everything else -- ownership, lifetimes, callbacks, multi-object operations -- stays
hand-written C++, and docs/c-api/GENERATED_ROUTES.md says how to tell which is which.

Inputs, all in the repository:

* ``modules/c-api/spec/<Family>.toml`` -- what each route does (see the format below).
* ``modules/c-api/include/CNA/C/*.h`` -- each route's signature. The public headers stay
  hand-written and remain the ABI's source of truth; the spec never repeats a signature.
* ``modules/c-api/src/CnaCApiMarshal.hpp`` -- which C value structs have ToNative/ToC conversions.

Output: ``modules/c-api/src/generated/<Family>.inc``, included at the end of
``modules/c-api/src/CnaCApi<Family>.cpp``. The routes there may use that file's hand-written
lookup helpers and its using-declarations for native types. Also written:
``modules/c-api/tests/pure_c/GeneratedRoutesSmoke.c``, the contract test of every generated route.

Spec format. Each top-level table is one group of routes sharing how they reach their object::

    [basic_effect]
    lookup = "GetStockEffect<BasicEffect>"  # CNA_Result Lookup(<first>, <object>*), hand-written
    object = "BasicEffect*"                 # the local the lookup fills; expressions call it `object`

    [basic_effect.routes]
    cna_basic_effect_get_alpha = { get = "object->getAlphaProperty()" }
    cna_basic_effect_set_alpha = { call = "object->setAlphaProperty(value)" }

A lookup receives the route's first parameter -- usually a handle, sometimes a struct or a string
it validates -- unconverted. A group without ``lookup`` holds routes that take no handle. A group
whose lookup only validates (``ValidateActiveGameHandle``) omits ``object``. Each route is ``name = { <kind> = "<C++>" }``:

``value``  (args..., T* out)          ``*out`` = the expression; no handle.
``get``    (handle, args..., T* out)   ``*out`` = the expression, after the lookup.
``call``   (handle, args...)           the expression is a statement run after the lookup.
``size``   ([handle,] args..., uint64_t* out_bytes)
                                       the byte count of the text the expression yields.
``copy``   ([handle,] args..., char* destination, uint64_t capacity, uint64_t* out_bytes)
                                       the size-then-copy text protocol for the expression.
``release`` (handle)                   a destroy with no bookkeeping: after the lookup, run the
                                       statement (``true`` for none) and release the handle.

Inside an expression every route parameter is available under its header name, already converted
to its native type: a struct CnaCApiMarshal.hpp converts through ToNative, and a CNA_Bool is
validated as canonical and read as ``bool``. Every other parameter is passed as declared. The
result is converted back by the output's C type: ToC for a marshaled struct, ToCBool for
CNA_Bool, and static_cast for an enumeration or a number.

Every generated route runs inside CallWithExceptionBarrier, refuses a null output with
CNA_RESULT_INVALID_ARGUMENT before doing anything else, validates CNA_Bool inputs before the
lookup, and returns whatever the lookup returns when the handle is refused.

Usage::

    python3 tools/c-api/generate_routes.py           # rewrite every generated file
    python3 tools/c-api/generate_routes.py --check   # fail if any generated file is stale
    python3 tools/c-api/generate_routes.py --list    # print every generated route
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
C_API_DIR = REPO_ROOT / "modules" / "c-api"
SPEC_DIR = C_API_DIR / "spec"
SOURCE_DIR = C_API_DIR / "src"
GENERATED_DIR = SOURCE_DIR / "generated"
HEADER_DIR = C_API_DIR / "include" / "CNA" / "C"
MARSHAL_HEADER = SOURCE_DIR / "CnaCApiMarshal.hpp"

DETAIL = "CNA::C::Detail::"
KINDS = ("value", "get", "call", "size", "copy", "release")
NUMBERS = {"float", "double", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t",
           "int64_t", "uint64_t", "size_t"}


class SpecError(Exception):
    """A spec that cannot be generated; the message names the file and the route."""


@dataclass(frozen=True)
class Param:
    type: str      # the C type without qualifiers or the pointer, e.g. "CNA_Vector3"
    name: str      # the header's parameter name
    pointer: bool
    const: bool    # pointer to const

    def declaration(self, name: str | None = None) -> str:
        spelled = name or self.name
        if self.pointer:
            return f"{'const ' if self.const else ''}{self.type}* const {spelled}"
        return f"const {self.type} {spelled}"


@dataclass(frozen=True)
class CTypes:
    """What the headers and the marshal header say about each C type."""
    handles: frozenset[str]
    enums: frozenset[str]
    marshaled: frozenset[str]
    routes: dict[str, list[Param]]

    def kind_of(self, type_: str) -> str:
        if type_ == "CNA_Bool":
            return "bool"
        if type_ in self.handles:
            return "handle"
        if type_ in self.marshaled:
            return "marshaled"
        if type_ in self.enums:
            return "enum"
        if type_ in NUMBERS:
            return "number"
        return "other"


def parse_param(text: str) -> Param:
    tokens = re.findall(r"\w+|\*", text)
    if len(tokens) < 2 or tokens[-1] in ("const", "*"):
        raise SpecError(f"cannot read the parameter '{text.strip()}'")
    stars = tokens.count("*")
    base = next(token for token in tokens if token != "const")
    # A pointer to a pointer is spelled into the type, so no conversion ever matches it.
    return Param(base + "*" * max(stars - 1, 0), tokens[-1], stars > 0,
                 tokens[0] == "const")


def read_c_types() -> CTypes:
    handles = {"CNA_Handle"}
    enums: set[str] = set()
    routes: dict[str, list[Param]] = {}
    for header in sorted(HEADER_DIR.glob("*.h")):
        text = re.sub(r"/\*.*?\*/", "", header.read_text(encoding="utf-8"), flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        for alias_of, name in re.findall(r"^\s*typedef\s+(\w+)\s+(CNA_\w+)\s*;", text, re.M):
            if alias_of == "CNA_Handle":
                handles.add(name)
            elif alias_of in NUMBERS and name != "CNA_Bool":
                enums.add(name)
        for name, params in re.findall(
                r"CNA_C_API\s+CNA_Result\s+(cna_\w+)\s*\(([^;]*?)\)\s*;", text, re.S):
            params = " ".join(params.split())
            routes[name] = ([] if params in ("", "void")
                            else [parse_param(part) for part in params.split(",")])
    marshal = MARSHAL_HEADER.read_text(encoding="utf-8")
    to_native = set(re.findall(r"ToNative\(\s*const (CNA_\w+)", marshal))
    to_c = set(re.findall(r"\b(CNA_\w+) ToC\(", marshal))
    return CTypes(frozenset(handles), frozenset(enums), frozenset(to_native & to_c), routes)


# ---- emitting one route ---------------------------------------------------------------------

def null_check(route: str, param: Param, indent: str) -> list[str]:
    return [f"{indent}if ({param.name} == nullptr) {{",
            f'{indent}    return {DETAIL}InvalidArgument("{route}: {param.name} is null.");',
            f"{indent}}}"]


def returns_on_failure(call: str, indent: str) -> list[str]:
    # camelCase locals cannot collide with the headers' snake_case parameter names.
    return [f"{indent}if (const CNA_Result routeResult = {call};",
            f"{indent}    routeResult != CNA_RESULT_SUCCESS) {{",
            f"{indent}    return routeResult;",
            f"{indent}}}"]


class Route:
    """One spec entry, checked against its header declaration."""

    def __init__(self, family: str, group: dict, name: str, entry: dict, types: CTypes):
        self.family, self.group, self.name, self.types = family, group, name, types
        if not isinstance(entry, dict) or len([k for k in entry if k in KINDS]) != 1:
            raise SpecError(f"{name}: write exactly one of {', '.join(KINDS)}, e.g. "
                            f'{{ get = "object->getXProperty()" }}')
        unknown = set(entry) - set(KINDS)
        if unknown:
            raise SpecError(f"{name}: unknown key(s) {', '.join(sorted(unknown))}")
        self.kind = next(k for k in entry if k in KINDS)
        self.expression = entry[self.kind]
        if self.kind == "release" and self.expression is True:
            self.expression = ""
        if not isinstance(self.expression, str):
            raise SpecError(f"{name}: '{self.kind}' takes a C++ expression in quotes")
        if name not in types.routes:
            raise SpecError(f"{name}: no public header declares this route; declare it first")
        self.params = types.routes[name]
        self.has_lookup = "lookup" in group
        self._check_shape()

    def _fail(self, problem: str) -> SpecError:
        declared = ", ".join(p.declaration() for p in self.params) or "void"
        return SpecError(f"{self.name}: {problem}; the header declares ({declared})")

    def _check_shape(self) -> None:
        params = list(self.params)
        if self.has_lookup:
            if not params:
                raise self._fail("a route in a group with a lookup passes it its first parameter")
            self.handle, params = params[0], params[1:]
        elif self.kind in ("get", "call", "release"):
            raise self._fail(f"'{self.kind}' needs a group with a lookup")
        else:
            self.handle = None
        self.outputs: list[Param] = []
        if self.kind in ("value", "get"):
            if not params or not params[-1].pointer or params[-1].const:
                raise self._fail(f"'{self.kind}' needs a final output pointer")
            self.outputs = [params.pop()]
            kind = self.types.kind_of(self.outputs[0].type)
            if kind not in ("marshaled", "bool", "enum", "number"):
                raise self._fail(f"'{self.kind}' cannot convert an output of type "
                                 f"{self.outputs[0].type}")
        elif self.kind == "size":
            if not params or params[-1].type != "uint64_t" or not params[-1].pointer:
                raise self._fail("'size' needs a final uint64_t* byte-count output")
            self.outputs = [params.pop()]
        elif self.kind == "release":
            if params or self.types.kind_of(self.handle.type) != "handle":
                raise self._fail("'release' takes only the handle it destroys")
        elif self.kind == "copy":
            tail = params[-3:]
            if (len(tail) != 3 or tail[0].type != "char" or not tail[0].pointer or tail[0].const
                    or tail[1].type != "uint64_t" or tail[1].pointer
                    or tail[2].type != "uint64_t" or not tail[2].pointer):
                raise self._fail("'copy' needs (char* destination, uint64_t capacity, "
                                 "uint64_t* out_bytes) last")
            self.outputs = params[-3:]
            params = params[:-3]
        for param in params:
            if param.pointer:
                raise self._fail(f"input '{param.name}' is a pointer, which this generator does "
                                 "not convert; write the route by hand")
            if self.types.kind_of(param.type) == "other":
                raise self._fail(f"input '{param.name}' has type {param.type}, which this "
                                 "generator does not convert; write the route by hand")
        self.inputs = params
        names = {p.name for p in self.params}
        locals_ = {"object", "routeResult"} | {f"c_{p.name}" for p in self.inputs}
        if names & locals_:
            raise self._fail(f"a parameter is named {', '.join(sorted(names & locals_))}, "
                             "which the generated code uses for a local")

    # The C parameter is renamed when the expression sees a converted local under its name.
    def _converted(self, param: Param) -> bool:
        return not param.pointer and self.types.kind_of(param.type) in ("marshaled", "bool")

    def _c_name(self, param: Param) -> str:
        return f"c_{param.name}" if self._converted(param) else param.name

    def signature(self) -> list[str]:
        params = [p.declaration(self._c_name(p)) for p in self.params]
        if not params:
            return [f"CNA_Result {self.name}(void)"]
        lines = [f"CNA_Result {self.name}("]
        lines += [f"    {p}," for p in params[:-1]] + [f"    {params[-1]})"]
        return lines

    def _bool_checks(self, indent: str) -> list[str]:
        lines: list[str] = []
        for param in self.inputs:
            if self.types.kind_of(param.type) == "bool":
                lines += returns_on_failure(
                    f'{DETAIL}ValidateCanonicalBool({self._c_name(param)}, "{param.name}")', indent)
        return lines

    def _locals(self, indent: str) -> list[str]:
        lines = []
        for param in self.inputs:
            kind = self.types.kind_of(param.type)
            if kind == "marshaled":
                lines.append(f"{indent}const auto {param.name} = "
                             f"{DETAIL}ToNative({self._c_name(param)});")
            elif kind == "bool":
                lines.append(f"{indent}const bool {param.name} = "
                             f"{DETAIL}FromCBool({self._c_name(param)});")
        return lines

    def _lookup(self, indent: str) -> list[str]:
        if not self.has_lookup:
            return []
        lookup = self.group["lookup"]
        if "object" not in self.group:
            return returns_on_failure(f"{lookup}({self.handle.name})", indent)
        return ([f"{indent}{self.group['object']} object{{}};"]
                + returns_on_failure(f"{lookup}({self.handle.name}, &object)", indent))

    def _converted_result(self) -> str:
        output = self.outputs[0]
        kind = self.types.kind_of(output.type)
        if kind == "marshaled":
            return f"{DETAIL}ToC({self.expression})"
        if kind == "bool":
            return f"{DETAIL}ToCBool({self.expression})"
        return f"static_cast<{output.type}>({self.expression})"

    def body(self) -> list[str]:
        i = " " * 8
        if self.kind == "copy" and not self.has_lookup:
            destination, capacity, out_bytes = (p.name for p in self.outputs)
            return ([f"    return {DETAIL}CopyFormattedText({destination}, {capacity}, "
                     f"{out_bytes}, [&] {{"]
                     + self._locals(i)
                     + [f"{i}return {self.expression};", "    });"])
        lines = [f"    return {DETAIL}CallWithExceptionBarrier([&]() -> CNA_Result {{"]
        if self.kind in ("value", "get", "size"):
            lines += null_check(self.name, self.outputs[0], i)
        lines += self._bool_checks(i)
        lines += self._lookup(i)
        lines += self._locals(i)
        if self.kind in ("value", "get"):
            lines += [f"{i}*{self.outputs[0].name} = {self._converted_result()};",
                      f"{i}return CNA_RESULT_SUCCESS;"]
        elif self.kind == "call":
            lines += [f"{i}{self.expression};", f"{i}return CNA_RESULT_SUCCESS;"]
        elif self.kind == "release":
            if self.expression:
                lines.append(f"{i}{self.expression};")
            lines.append(f"{i}return {DETAIL}ReleaseHandle({self.handle.name});")
        elif self.kind == "size":
            lines += [f"{i}return {DETAIL}ReportTextSize({self.expression}, "
                      f"{self.outputs[0].name});"]
        else:
            destination, capacity, out_bytes = (p.name for p in self.outputs)
            lines += [f"{i}return {DETAIL}CopyText({self.expression}, {destination}, "
                      f"{capacity}, {out_bytes});"]
        return lines + ["    });"]

    def render(self) -> str:
        return "\n".join(self.signature() + ["{"] + self.body() + ["}"]) + "\n"


# ---- one spec file --------------------------------------------------------------------------

def load_family(spec: Path, types: CTypes) -> list[Route]:
    family = spec.stem
    try:
        data = tomllib.loads(spec.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as error:
        raise SpecError(f"{spec.name}: {error}") from error
    routes = []
    for group_name, group in data.items():
        if not isinstance(group, dict) or "routes" not in group:
            raise SpecError(f"{spec.name}: [{group_name}] has no [{group_name}.routes] table")
        unknown = set(group) - {"lookup", "object", "routes"}
        if unknown:
            raise SpecError(f"{spec.name}: [{group_name}] has unknown key(s) "
                            f"{', '.join(sorted(unknown))}")
        if "object" in group and "lookup" not in group:
            raise SpecError(f"{spec.name}: [{group_name}] names an object but no lookup")
        for name, entry in group["routes"].items():
            try:
                routes.append(Route(family, group, name, entry, types))
            except SpecError as error:
                raise SpecError(f"{spec.name}: {error}") from error
    return routes


def render_family(spec: Path, routes: list[Route]) -> str:
    owner = f"CnaCApi{spec.stem}.cpp"
    header = [
        "// SPDX-License-Identifier: MS-PL",
        "//",
        "// GENERATED FILE. DO NOT EDIT.",
        "// Generated by tools/c-api/generate_routes.py from "
        f"modules/c-api/spec/{spec.name}.",
        f"// Included at the end of src/{owner}, whose lookups and native types these routes use.",
        "// To change a route, edit the spec and regenerate; docs/c-api/GENERATED_ROUTES.md.",
        "",
        '#include "../CnaCApiDetail.hpp"',
        '#include "../CnaCApiMarshal.hpp"',
        '#include "../CnaCApiOutput.hpp"',
        "",
        "",
    ]
    return "\n".join(header) + "\n".join(route.render() for route in routes)


# ---- the contract test --------------------------------------------------------------------

CONTRACT_TEST = C_API_DIR / "tests" / "pure_c" / "GeneratedRoutesSmoke.c"


def _arguments(route: Route, handle: str | None = None, **overrides: str) -> str:
    """C arguments for one call: zeroed stand-ins and valid outputs unless `overrides` says else.

    `handle` replaces a lookup's handle parameter; `overrides` maps a parameter name to its spelling.
    """
    out = []
    for param in route.params:
        if param.name in overrides:
            out.append(overrides[param.name])
        elif route.has_lookup and param is route.handle:
            out.append(handle if handle is not None
                       else "NULL" if param.pointer else f"v_{param.type}")
        elif route.kind == "copy" and param in route.outputs:
            destination, capacity, _ = route.outputs
            out.append("text" if param is destination
                       else "(uint64_t)sizeof(text)" if param is capacity else "&o_uint64_t")
        elif param in route.outputs:
            out.append(f"&o_{param.type}")
        else:
            out.append(f"v_{param.type}")
    return ", ".join(out)


def _expect(call: str, wanted: str, route: str, contract: str) -> str:
    return f'    failures += expect({call}, {wanted}, "{route}", {contract});'


def _takes_handle(route: Route) -> bool:
    return (route.has_lookup and not route.handle.pointer
            and route.types.kind_of(route.handle.type) == "handle")


def render_contract_test(routes: list[Route]) -> str:
    """The mechanical contracts every generated route promises, checked from C."""
    lines = [
        "// SPDX-License-Identifier: MS-PL",
        "//",
        "// GENERATED FILE. DO NOT EDIT.",
        "// Generated by tools/c-api/generate_routes.py from modules/c-api/spec/*.toml.",
        "//",
        "// The contracts every generated route promises, whatever its spec line says: a null output",
        "// is refused before anything else, a handle that is null, of another kind or destroyed is",
        "// refused with CNA_RESULT_INVALID_HANDLE, and the value text routes keep the size-then-copy",
        "// protocol. What each route computes is the business of the family's own smoke test.",
        "",
        "#include <CNA/C/cna.h>",
        "",
        "#include <stdint.h>",
        "#include <stdio.h>",
        "#include <string.h>",
        "",
        "/* Zeroed stand-ins for every argument and output; static, so they start zero. */",
        "@STANDINS@",
        "static char text[512];",
        "",
        "static int expect(const CNA_Result result, const CNA_Result wanted, const char* const route,",
        "                  const char* const contract)",
        "{",
        "    if (result == wanted) {",
        "        return 0;",
        "    }",
        '    printf("%s: %s answered %u, expected %u\\n", route, contract, (unsigned)result,',
        "           (unsigned)wanted);",
        "    return 1;",
        "}",
        "",
        "static int null_outputs_are_refused(void)",
        "{",
        "    int failures = 0;",
    ]
    for r in routes:
        if r.kind in ("value", "get", "size"):
            output = r.outputs[0].name
            lines.append(_expect(f"{r.name}({_arguments(r, **{output: 'NULL'})})",
                                 "CNA_RESULT_INVALID_ARGUMENT", r.name, f'"a null {output}"'))
        elif r.kind == "copy" and not r.has_lookup:
            destination, capacity, out_bytes = (p.name for p in r.outputs)
            lines.append(_expect(f"{r.name}({_arguments(r, **{destination: 'NULL', capacity: '1U'})})",
                                 "CNA_RESULT_INVALID_ARGUMENT", r.name,
                                 '"a null destination with a capacity"'))
            lines.append(_expect(f"{r.name}({_arguments(r, **{out_bytes: 'NULL'})})",
                                 "CNA_RESULT_INVALID_ARGUMENT", r.name, '"a null out_bytes"'))
    lines += ["    return failures;", "}", "",
              "/* `handle` goes to every route but the curve routes, which get `curve_routes_handle`. */",
              "static int handles_are_refused(const CNA_Handle handle,",
              "                               const CNA_Handle curve_routes_handle,",
              "                               const char* const what)",
              "{", "    int failures = 0;"]
    for r in routes:
        if _takes_handle(r):
            handle = "curve_routes_handle" if r.handle.type == "CNA_CurveHandle" else "handle"
            lines.append(_expect(f"{r.name}({_arguments(r, handle)})",
                                 "CNA_RESULT_INVALID_HANDLE", r.name, "what"))
    lines += ["    return failures;", "}", "",
              "static int text_protocol_holds(void)", "{",
              "    int failures = 0;", "    uint64_t size = 0U;"]
    copies = {(r.group.get("lookup"), r.expression, tuple(r.inputs)): r
              for r in routes if r.kind == "copy" and not r.has_lookup}
    for r in routes:
        pair = copies.get((r.group.get("lookup"), r.expression, tuple(r.inputs)))
        if r.kind != "size" or r.has_lookup or pair is None:
            continue
        size_args = _arguments(r, **{r.outputs[0].name: "&size"})
        capacity = pair.outputs[1].name
        short = _arguments(pair, **{capacity: "size - 1U"})
        exact = _arguments(pair, **{capacity: "size"})
        lines += [
            _expect(f"{r.name}({size_args})", "CNA_RESULT_SUCCESS", r.name, '"zeroed input"'),
            "    if (size > 0U && size <= sizeof(text)) {",
            "        memset(text, 0x5A, sizeof(text));",
            "    " + _expect(f"{pair.name}({short})", "CNA_RESULT_BUFFER_TOO_SMALL", pair.name,
                             '"one byte short"'),
            f"        if (o_uint64_t != size || text[0] != 0x5A) {{",
            f'            printf("{pair.name}: a short buffer was written or the size not reported\\n");',
            "            ++failures;",
            "        }",
            "    " + _expect(f"{pair.name}({exact})", "CNA_RESULT_SUCCESS", pair.name,
                             '"an exact buffer"'),
            "    }",
        ]
    lines += ["    return failures;", "}", "",
              "int main(void)", "{",
              "    int failures = null_outputs_are_refused();",
              "    CNA_CurveHandle curve = CNA_INVALID_HANDLE;",
              "    CNA_PacketWriterHandle writer = CNA_INVALID_HANDLE;",
              "",
              "    failures += handles_are_refused(CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, "
              '"the null handle");',
              "    /* Live handles of a kind the route does not take. */",
              "    if (cna_curve_create(&curve) != CNA_RESULT_SUCCESS ||",
              "        cna_packet_writer_create(16, &writer) != CNA_RESULT_SUCCESS) {",
              '        printf("could not create the decoy handles\\n");',
              "        return 1;",
              "    }",
              '    failures += handles_are_refused(curve, writer, "a live handle of another kind");',
              "    /* Handles whose objects are gone. */",
              "    if (cna_curve_destroy(curve) != CNA_RESULT_SUCCESS ||",
              "        cna_packet_writer_destroy(writer) != CNA_RESULT_SUCCESS) {",
              '        printf("could not destroy the decoy handles\\n");',
              "        return 1;",
              "    }",
              '    failures += handles_are_refused(writer, curve, "a destroyed handle");',
              '    failures += handles_are_refused(curve, writer, "a destroyed handle");',
              "    failures += text_protocol_holds();",
              "",
              f'    printf("%d failed checks across {len(routes)} generated routes\\n", failures);',
              "    return failures == 0 ? 0 : 1;", "}", ""]
    types = {param.type for route in routes for param in route.params}
    used = sorted(set(re.findall(r"\b([vo])_(\w+)", "\n".join(lines))))
    standins = [f"static {name} {kind}_{name};" for kind, name in used if name in types]
    return "\n".join(lines).replace("@STANDINS@", "\n".join(standins))


def generate() -> tuple[dict[Path, str], list[Route]]:
    types = read_c_types()
    outputs: dict[Path, str] = {}
    every: list[Route] = []
    seen: dict[str, str] = {}
    for spec in sorted(SPEC_DIR.glob("*.toml")):
        routes = load_family(spec, types)
        for route in routes:
            if route.name in seen:
                raise SpecError(f"{route.name} is specified in both {seen[route.name]} and "
                                f"{spec.name}")
            seen[route.name] = spec.name
        owner = SOURCE_DIR / f"CnaCApi{spec.stem}.cpp"
        include = f'#include "generated/{spec.stem}.inc"'
        if not owner.exists() or include not in owner.read_text(encoding="utf-8"):
            raise SpecError(f"{spec.name}: src/{owner.name} must end with {include}")
        outputs[GENERATED_DIR / f"{spec.stem}.inc"] = render_family(spec, routes)
        every += routes
    outputs[CONTRACT_TEST] = render_contract_test(every)
    return outputs, every


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n", 1)[0],
        formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true",
                      help="fail when a generated file differs from what the specs produce")
    mode.add_argument("--list", action="store_true", help="print every generated route")
    args = parser.parse_args()

    try:
        outputs, routes = generate()
    except SpecError as error:
        print(f"generate_routes.py: {error}", file=sys.stderr)
        return 2

    if args.list:
        for route in routes:
            print(f"{route.family}\t{route.kind}\t{route.name}")
        return 0

    stale = [path for path, text in outputs.items()
             if not path.exists() or path.read_text(encoding="utf-8") != text]
    orphans = [path for path in sorted(GENERATED_DIR.glob("*.inc")) if path not in outputs]
    if args.check:
        for path in stale:
            print(f"{path.relative_to(REPO_ROOT)} is stale")
        for path in orphans:
            print(f"{path.relative_to(REPO_ROOT)} has no spec")
        if stale or orphans:
            print("Run python3 tools/c-api/generate_routes.py and commit the result.")
            return 1
        print(f"{len(routes)} generated routes in {len(outputs)} files are current")
        return 0

    GENERATED_DIR.mkdir(exist_ok=True)
    for path in stale:
        path.write_text(outputs[path], encoding="utf-8", newline="\n")
        print(f"wrote {path.relative_to(REPO_ROOT)}")
    for path in orphans:
        path.unlink()
        print(f"removed {path.relative_to(REPO_ROOT)}")
    print(f"{len(routes)} generated routes in {len(outputs)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
