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
it validates -- unconverted; ``arguments = 2`` in the group passes it the first two (an index,
say). A lookup that hands back a pointer into a resource it must keep alive names that resource's
type as ``owner``: it is then called as ``Lookup(<first>, &owner, &object)`` and expressions may
use ``owner`` too. A route over two objects -- equals, contains, index_of -- takes a second handle
right after the lookup's parameters; its group says how to reach that one with
``other = { lookup = "...", object = "..." }``, looked up after the first into ``other``.
A group without ``lookup`` holds routes that take no handle. A group whose lookup only validates
(``ValidateActiveGameHandle``) omits ``object``. A group whose lookup reads the null handle as
"none" says ``accepts_null_handle = true``. A group whose routes exist only when a macro is
defined says ``feature = "CNA_DEVICES"`` and ``unavailable = "<C++ result>"``, what they return
without it; a handle output is cleared there too.

Each route is ``name = { <kind> = "<C++>" }``:

``value``  (args..., T* out)          ``*out`` = the expression; no handle.
``get``    (handle, args..., T* out)   ``*out`` = the expression, after the lookup.
``call``   ([handle,] args...)         the expression is a statement run after the lookup, if any.
``size``   ([handle,] args..., uint64_t* out_bytes)
                                       the byte count of the text the expression yields.
``copy``   ([handle,] args..., char* destination, uint64_t capacity, uint64_t* out_bytes)
                                       the size-then-copy text protocol for the expression; with
                                       a uint8_t* destination, the same protocol for its bytes.
``release`` (handle)                   a destroy with no bookkeeping: after the lookup, run the
                                       statement (``true`` for none) and release the handle.
``result`` (anything)                  return what the expression returns, a CNA_Result -- the
                                       route is a call into a hand-written helper. Every non-const
                                       ``out_*`` pointer is refused when null before anything else,
                                       and a handle output is first set to CNA_INVALID_HANDLE.
                                       Outputs listed in ``optional = ["out_x"]`` may be null:
                                       they are not refused, a handle among them is cleared only
                                       when given, and the helper decides what null means.

Inside an expression every route parameter is available under its header name, already converted
to its native type: a struct CnaCApiMarshal.hpp converts through ToNative, a CNA_Bool is
validated as canonical and read as ``bool``, and a CNA_StringView is read as UTF-8 into a
``std::string`` (CNA_RESULT_ENCODING when it is not; ``refuse_nul = ["name"]`` also refuses an
embedded NUL). A lookup's own parameters, and the text inputs of a ``result`` route (its helper
reads them), are passed unconverted. Every other parameter is passed as declared. The
result is converted back by the output's C type: ToC for a marshaled struct, ToCBool for
CNA_Bool, static_cast for an enumeration or a number; any other C structure is assigned as the
expression builds it. A group that says ``raw = true`` works on the C values themselves: its
struct and CNA_Bool parameters are passed as declared (a CNA_Bool still checked canonical first)
and a struct result is assigned as built.

A ``value`` or ``get`` whose output is a versioned structure (``struct_size``, ``struct_version``)
says ``versioned = true``: the output is then refused unless it is a version 1 structure large
enough for this build, rather than only when it is null.

A route may add ``invalid_if = "<C++ condition>"`` (or a list of them): an argument the route
refuses with CNA_RESULT_INVALID_ARGUMENT, e.g. ``invalid_if = "!std::isfinite(volume)"``. The
condition reads the parameters as the caller passed them, before any conversion, and is checked
before the lookup. A check that needs its own result code or message is a call instead,
``validate = "ValidateKey(key)"`` (or a list): any CNA_Result but success is returned as it is.
An input structure passed by const pointer is accepted only when a condition or a validate call
names it and so refuses an invalid one, null included (``!IsPresentationParameters(source)``);
the contract test holds it to refusing null. A check that reads the looked-up object -- an index
against its count -- is ``require = "RequireIndex(index, object->value->bones.size(), ...)"``,
the same kind of call made after the lookup.

By default a generated route runs inside CallWithExceptionBarrier. A group with ``dispatch = false``
uses CallHereWithExceptionBarrier: exceptions are still translated, but an owner-thread refusal is
never queued for retry. Use it for game-driving, destruction and dispatch-control routes whose
contract requires the caller itself to be the owner. Every route refuses a null output (for ``copy``, a
null ``out_bytes`` or a null destination with a capacity) with CNA_RESULT_INVALID_ARGUMENT before
doing anything else, then validates CNA_Bool inputs, then its
``invalid_if`` conditions and ``validate`` calls, then reads its text, and returns whatever the
lookup returns when the handle is refused; ``require`` calls come last, before the work.

Usage::

    python3 tools/c-api/generate_routes.py           # rewrite every generated file
    python3 tools/c-api/generate_routes.py --check   # fail if any generated file is stale
    python3 tools/c-api/generate_routes.py --list    # print every generated route
"""

from __future__ import annotations

import argparse
import re
import sys
import textwrap
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
KINDS = ("value", "get", "call", "size", "copy", "release", "result")
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
        if type_ == "CNA_StringView":
            return "text"
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


def expand_lines(lines: list[str]) -> list[str]:
    """Lines holding a multi-line expression, its continuation lines indented under the first."""
    out = []
    for line in lines:
        first, *rest = line.split("\n")
        indent = first[:len(first) - len(first.lstrip())]
        out += [first] + [indent + part if part.strip() else "" for part in rest]
    return out


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
        unknown = set(entry) - set(KINDS) - {"invalid_if", "validate", "require", "versioned",
                                             "refuse_nul", "optional"}
        if unknown:
            raise SpecError(f"{name}: unknown key(s) {', '.join(sorted(unknown))}")
        refusals = entry.get("invalid_if", [])
        self.refusals = [refusals] if isinstance(refusals, str) else refusals
        if not isinstance(self.refusals, list) or not all(
                isinstance(r, str) and r for r in self.refusals):
            raise SpecError(f"{name}: invalid_if takes a C++ condition in quotes, or a list of them")
        validations = entry.get("validate", [])
        self.validations = [validations] if isinstance(validations, str) else validations
        if not isinstance(self.validations, list) or not all(
                isinstance(v, str) and v for v in self.validations):
            raise SpecError(f"{name}: validate takes a C++ call returning CNA_Result, or a list")
        requirements = entry.get("require", [])
        self.requirements = [requirements] if isinstance(requirements, str) else requirements
        if not isinstance(self.requirements, list) or not all(
                isinstance(r, str) and r for r in self.requirements):
            raise SpecError(f"{name}: require takes a C++ call returning CNA_Result, or a list")
        self.refuse_nul = entry.get("refuse_nul", [])
        if not isinstance(self.refuse_nul, list) or not all(
                isinstance(n, str) for n in self.refuse_nul):
            raise SpecError(f"{name}: refuse_nul takes a list of text parameter names")
        self.optional = entry.get("optional", [])
        if not isinstance(self.optional, list) or not all(
                isinstance(n, str) for n in self.optional):
            raise SpecError(f"{name}: optional takes a list of output parameter names")
        self.versioned = entry.get("versioned", False)
        if self.versioned is not False and self.versioned is not True:
            raise SpecError(f"{name}: versioned is either true or left out")
        self.kind = next(k for k in entry if k in KINDS)
        self.expression = entry[self.kind]
        if self.kind == "release" and self.expression is True:
            self.expression = ""
        if not isinstance(self.expression, str):
            raise SpecError(f"{name}: '{self.kind}' takes a C++ expression in quotes")
        # A long expression may span lines (a TOML ''' string); it keeps its own layout.
        self.expression = textwrap.dedent(self.expression).strip("\n")
        if name not in types.routes:
            raise SpecError(f"{name}: no public header declares this route; declare it first")
        self.params = types.routes[name]
        self.has_lookup = "lookup" in group
        self.other: Param | None = None
        self._check_shape()
        readable_text = {p.name for p in self.inputs
                         if p.type == "CNA_StringView" and self._converted(p)}
        if not set(self.refuse_nul) <= readable_text:
            raise SpecError(f"{name}: refuse_nul must name text inputs read by this route; "
                            "a lookup or result helper validates its own text")
        if self.requirements and not self.has_lookup:
            raise SpecError(f"{name}: require reads the looked-up object; without a lookup, "
                            "write validate")
        if self.optional and (self.kind != "result"
                              or not set(self.optional) <= {o.name for o in self.outputs}):
            raise SpecError(f"{name}: optional names out_* outputs of a 'result' route")
        if self.versioned and self.kind not in ("value", "get"):
            raise SpecError(f"{name}: only a 'value' or 'get' output can be versioned")

    def _fail(self, problem: str) -> SpecError:
        declared = ", ".join(p.declaration() for p in self.params) or "void"
        return SpecError(f"{self.name}: {problem}; the header declares ({declared})")

    def _check_shape(self) -> None:
        params = list(self.params)
        if self.has_lookup:
            count = self.group.get("arguments", 1)
            if len(params) < count:
                raise self._fail(f"a route in this group passes its lookup {count} parameter(s)")
            self.handle, self.lookup_params, params = params[0], params[:count], params[count:]
            if "other" in self.group:
                if (not params or params[0].pointer
                        or self.types.kind_of(params[0].type) != "handle"):
                    raise self._fail("a route in this group takes a second handle after the "
                                     "lookup's parameters")
                self.other, params = params[0], params[1:]
        elif self.kind in ("get", "release"):
            raise self._fail(f"'{self.kind}' needs a group with a lookup")
        else:
            self.handle = None
        self.outputs: list[Param] = []
        if self.kind == "result":
            self.outputs = [p for p in params if p.pointer and not p.const
                            and p.name.startswith("out_")]
            self.inputs = [p for p in params if not p.pointer]
            self._check_names()
            return
        if self.kind in ("value", "get"):
            if not params or not params[-1].pointer or params[-1].const:
                raise self._fail(f"'{self.kind}' needs a final output pointer")
            self.outputs = [params.pop()]
            kind = self.types.kind_of(self.outputs[0].type)
            if kind == "handle" or "*" in self.outputs[0].type:
                raise self._fail(f"'{self.kind}' does not return handles; a handle output "
                                 "decides ownership, so write it by hand or as a 'result'")
        elif self.kind == "size":
            if not params or params[-1].type != "uint64_t" or not params[-1].pointer:
                raise self._fail("'size' needs a final uint64_t* byte-count output")
            self.outputs = [params.pop()]
        elif self.kind == "release":
            if params or self.types.kind_of(self.handle.type) != "handle":
                raise self._fail("'release' takes only the handle it destroys")
        elif self.kind == "copy":
            tail = params[-3:]
            if (len(tail) != 3 or tail[0].type not in ("char", "uint8_t") or not tail[0].pointer
                    or tail[0].const or tail[1].type != "uint64_t" or tail[1].pointer
                    or tail[2].type != "uint64_t" or not tail[2].pointer):
                raise self._fail("'copy' needs (char* or uint8_t* destination, uint64_t "
                                 "capacity, uint64_t* out_bytes) last")
            self.outputs = params[-3:]
            params = params[:-3]
        for param in params:
            if param.pointer:
                # A structure read through a pointer is passed as declared, and only when a
                # refusal names it: that condition is what keeps a null one from being read.
                if not param.const or not any(re.search(rf"\b{param.name}\b", check)
                                              for check in self.refusals + self.validations):
                    raise self._fail(f"input '{param.name}' is a pointer; write the route by "
                                     "hand, or refuse an invalid one (null included) with an "
                                     "invalid_if or validate that names it")
                continue
            if self.types.kind_of(param.type) == "other" and not self.group.get("raw"):
                raise self._fail(f"input '{param.name}' has type {param.type}, which this "
                                 "generator does not convert; write the route by hand, or "
                                 "in a raw group that takes C structures as declared")
        self.inputs = params
        self._check_names()

    def _check_names(self) -> None:
        names = {p.name for p in self.params if not self._renamed(p)}
        # `object`, `owner` and `other` are locals only in groups that look something up.
        locals_ = ({"routeResult"} | {f"c_{p.name}" for p in self.inputs}
                   | ({"object", "owner", "other"} if self.has_lookup else set()))
        if names & locals_:
            raise self._fail(f"a parameter is named {', '.join(sorted(names & locals_))}, "
                             "which the generated code uses for a local")

    # The C parameter is renamed when the expression sees a converted local under its name.
    def _converted(self, param: Param) -> bool:
        kind = self.types.kind_of(param.type)
        if kind in ("marshaled", "bool") and self.group.get("raw"):
            return False  # checked like any other, then passed as the caller declared it
        if self.has_lookup and param in self.lookup_params:
            return False  # a lookup receives its parameters as the caller passed them
        if kind == "text" and self.kind == "result":
            return False  # the helper a `result` calls reads its own text
        return not param.pointer and kind in ("marshaled", "bool", "text")

    # The second handle may itself be named `other`; the definition renames it, as the ABI allows.
    def _renamed(self, param: Param) -> bool:
        return param is self.other and param.name == "other"

    def _c_name(self, param: Param) -> str:
        return f"c_{param.name}" if self._converted(param) or self._renamed(param) else param.name

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

    def _as_passed(self, code: str) -> str:
        """`code` over the parameters exactly as the caller passed them, before conversion."""
        for param in self.params:
            if self._converted(param):
                code = re.sub(rf"(?<![\w.>:]){param.name}(?!\w)", self._c_name(param), code)
        return code

    def _refusal_checks(self, indent: str) -> list[str]:
        """The `invalid_if` conditions, then the `validate` calls, over the parameters as passed."""
        lines: list[str] = []
        for condition in self.refusals:
            c_condition = self._as_passed(condition)
            message = condition.replace("\\", "\\\\").replace('"', '\\"')
            lines += [f"{indent}if ({c_condition}) {{",
                      f'{indent}    return {DETAIL}InvalidArgument("{self.name}: invalid because '
                      f'{message}.");',
                      f"{indent}}}"]
        for validation in self.validations:
            lines += returns_on_failure(self._as_passed(validation), indent)
        return lines

    def _text_reads(self, indent: str) -> list[str]:
        """Each CNA_StringView input read once, as std::string under its own name."""
        lines: list[str] = []
        for param in self.inputs:
            if self.types.kind_of(param.type) != "text" or not self._converted(param):
                continue
            rule = f", {DETAIL}EmbeddedNul::Refused" if param.name in self.refuse_nul else ""
            lines.append(f"{indent}std::string {param.name};")
            lines += returns_on_failure(
                f'{DETAIL}ReadText({self._c_name(param)}, "{self.name}: {param.name} is not '
                f'valid text.", &{param.name}{rule})', indent)
        return lines

    def _locals(self, indent: str) -> list[str]:
        lines = []
        for param in self.inputs:
            kind = self.types.kind_of(param.type)
            if kind == "marshaled" and self._converted(param):
                lines.append(f"{indent}const auto {param.name} = "
                             f"{DETAIL}ToNative({self._c_name(param)});")
            elif kind == "bool" and self._converted(param):
                lines.append(f"{indent}const bool {param.name} = "
                             f"{DETAIL}FromCBool({self._c_name(param)});")
        return lines

    def _lookup(self, indent: str) -> list[str]:
        if not self.has_lookup:
            return []
        lookup = self.group["lookup"]
        arguments = ", ".join(p.name for p in self.lookup_params)
        if "object" not in self.group:
            return returns_on_failure(f"{lookup}({arguments})", indent)
        if "owner" in self.group:
            lines = ([f"{indent}{self.group['owner']} owner{{}};",
                      f"{indent}{self.group['object']} object{{}};"]
                     + returns_on_failure(f"{lookup}({arguments}, &owner, &object)", indent))
        else:
            lines = ([f"{indent}{self.group['object']} object{{}};"]
                     + returns_on_failure(f"{lookup}({arguments}, &object)", indent))
        if self.other is not None:
            other = self.group["other"]
            lines += ([f"{indent}{other['object']} other{{}};"]
                      + returns_on_failure(f"{other['lookup']}({self._c_name(self.other)}, &other)",
                                           indent))
        return lines

    def _converted_result(self) -> str:
        output = self.outputs[0]
        kind = self.types.kind_of(output.type)
        if kind == "marshaled":
            if self.group.get("raw"):
                return self.expression  # the C structure itself, built by the expression
            return f"{DETAIL}ToC({self.expression})"
        if kind == "bool":
            return f"{DETAIL}ToCBool({self.expression})"
        if kind == "other":
            return self.expression  # a C structure the expression already builds
        return f"static_cast<{output.type}>({self.expression})"

    def body(self) -> list[str]:
        i = " " * 8
        reads_text = any(self.types.kind_of(p.type) == "text" for p in self.inputs)
        if (self.kind == "copy" and not self.has_lookup and not reads_text
                and self.group.get("dispatch", True)
                and not self.refusals and not self.validations
                and not any(p.type == "CNA_Bool" for p in self.inputs)
                and self.outputs[0].type == "char"):
            destination, capacity, out_bytes = (p.name for p in self.outputs)
            return ([f"    return {DETAIL}CopyFormattedText({destination}, {capacity}, "
                     f"{out_bytes}, [&] {{"]
                     + self._locals(i)
                     + [f"{i}return {self.expression};", "    });"])
        barrier = ("CallWithExceptionBarrier" if self.group.get("dispatch", True)
                   else "CallHereWithExceptionBarrier")
        lines = [f"    return {DETAIL}{barrier}([&]() -> CNA_Result {{"]
        if self.versioned:
            output = self.outputs[0].name
            lines += returns_on_failure(
                f'{DETAIL}ValidateVersionedStructure({output}, "{self.name}: {output} is null or '
                f'not a version 1 structure.")', i)
        elif self.kind in ("value", "get", "size"):
            lines += null_check(self.name, self.outputs[0], i)
        elif self.kind == "copy":
            lines += returns_on_failure(
                f"{DETAIL}ValidateCopyOutput({', '.join(p.name for p in self.outputs)})", i)
        elif self.kind == "result":
            for output in self.outputs:
                if output.name not in self.optional:
                    lines += null_check(self.name, output, i)
            for output in self.outputs:
                if self.types.kind_of(output.type) == "handle":
                    if output.name in self.optional:
                        lines += [f"{i}if ({output.name} != nullptr) {{",
                                  f"{i}    *{output.name} = CNA_INVALID_HANDLE;", f"{i}}}"]
                    else:
                        lines.append(f"{i}*{output.name} = CNA_INVALID_HANDLE;")
        lines += self._bool_checks(i)
        lines += self._refusal_checks(i)
        lines += self._text_reads(i)
        lines += self._lookup(i)
        lines += self._locals(i)
        for requirement in self.requirements:
            lines += returns_on_failure(requirement, i)
        if self.kind in ("value", "get"):
            lines += [f"{i}*{self.outputs[0].name} = {self._converted_result()};",
                      f"{i}return CNA_RESULT_SUCCESS;"]
        elif self.kind == "call":
            lines += [f"{i}{self.expression};", f"{i}return CNA_RESULT_SUCCESS;"]
        elif self.kind == "release":
            if self.expression:
                lines.append(f"{i}{self.expression};")
            lines.append(f"{i}return {DETAIL}ReleaseHandle({self.handle.name});")
        elif self.kind == "result":
            lines.append(f"{i}return {self.expression};")
        elif self.kind == "size":
            lines += [f"{i}return {DETAIL}ReportTextSize({self.expression}, "
                      f"{self.outputs[0].name});"]
        else:
            destination, capacity, out_bytes = (p.name for p in self.outputs)
            copy = "CopyText" if self.outputs[0].type == "char" else "CopyBytes"
            lines += [f"{i}return {DETAIL}{copy}({self.expression}, {destination}, "
                      f"{capacity}, {out_bytes});"]
        return expand_lines(lines + ["    });"])

    def unavailable_body(self) -> list[str]:
        """The stub a build without the group's feature compiles instead."""
        lines = [f"    (void){self._c_name(p)};" for p in self.params]
        for output in self.outputs:
            if output.pointer and self.types.kind_of(output.type) == "handle":
                lines += [f"    if ({output.name} != nullptr) {{",
                          f"        *{output.name} = CNA_INVALID_HANDLE;", "    }"]
        return lines + [f"    return {self.group['unavailable']};"]

    def render(self) -> str:
        real = self.signature() + ["{"] + self.body() + ["}"]
        if "feature" not in self.group:
            return "\n".join(real) + "\n"
        stub = self.signature() + ["{"] + self.unavailable_body() + ["}"]
        return "\n".join([f"#if defined({self.group['feature']})"] + real + ["#else"] + stub
                          + ["#endif"]) + "\n"


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
        unknown = set(group) - {"lookup", "object", "owner", "other", "arguments",
                                "accepts_null_handle", "feature", "unavailable", "raw", "routes",
                                "dispatch"}
        if unknown:
            raise SpecError(f"{spec.name}: [{group_name}] has unknown key(s) "
                            f"{', '.join(sorted(unknown))}")
        if not isinstance(group["routes"], dict):
            raise SpecError(f"{spec.name}: [{group_name}.routes] must be a table")
        if "dispatch" in group and type(group["dispatch"]) is not bool:
            raise SpecError(f"{spec.name}: [{group_name}] dispatch must be a boolean")
        for key in ("lookup", "object", "owner", "feature", "unavailable"):
            if key in group and (not isinstance(group[key], str) or not group[key].strip()):
                raise SpecError(f"{spec.name}: [{group_name}] {key} must be non-empty text")
        if ("accepts_null_handle" in group
                and (type(group["accepts_null_handle"]) is not bool or "lookup" not in group)):
            raise SpecError(f"{spec.name}: [{group_name}] accepts_null_handle needs a lookup "
                            "and a boolean")
        if ("object" in group or "arguments" in group) and "lookup" not in group:
            raise SpecError(f"{spec.name}: [{group_name}] names an object or arguments but no lookup")
        if ("owner" in group or "other" in group) and "object" not in group:
            raise SpecError(f"{spec.name}: [{group_name}] names an owner or other but no object")
        if "other" in group and (not isinstance(group["other"], dict)
                                 or set(group["other"]) != {"lookup", "object"}
                                 or not all(isinstance(v, str) and v.strip()
                                            for v in group["other"].values())):
            raise SpecError(f"{spec.name}: [{group_name}] other must be "
                            "{ lookup = \"...\", object = \"...\" }")
        if "raw" in group and group["raw"] is not True:
            raise SpecError(f"{spec.name}: [{group_name}] raw is either true or left out")
        if ("feature" in group) != ("unavailable" in group):
            raise SpecError(f"{spec.name}: [{group_name}] needs both feature and unavailable")
        if "arguments" in group and (type(group["arguments"]) is not int or group["arguments"] < 1):
            raise SpecError(f"{spec.name}: [{group_name}] arguments must be a positive number")
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


def _standin(kind: str, type_: str) -> str:
    """The name of the zeroed static standing in for a value or an output of `type_`."""
    return f"{kind}_{re.sub(r'[^0-9A-Za-z_]', '_', type_)}"


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
                       else "NULL" if param.pointer else _standin("v", param.type))
        elif route.kind == "copy" and param in route.outputs:
            destination, capacity, _ = route.outputs
            bytes_ = destination.type == "uint8_t"
            out.append(("(uint8_t*)text" if bytes_ else "text") if param is destination
                       else "(uint64_t)sizeof(text)" if param is capacity else "&o_uint64_t")
        elif param in route.outputs:
            out.append("&" + _standin("o", param.type))
        elif param.pointer:
            out.append("NULL")
        else:
            out.append(_standin("v", param.type))
    return ", ".join(out)


def _expect(call: str, wanted: str, route: str, contract: str, gated: bool = False,
            checks_arguments: bool = False) -> str:
    if gated and checks_arguments:
        raise SpecError(f"{route}: a route behind a feature takes no invalid_if yet")
    check = ("expect_unless_compiled_out" if gated
             else "expect_unless_arguments_refused" if checks_arguments else "expect")
    return f'    failures += {check}({call}, {wanted}, "{route}", {contract});'


def _versioned_header(route: Route, version: str) -> list[str]:
    """Stamps the shared output stand-in of a versioned route with a header of `version`."""
    output = route.outputs[0]
    standin = _standin("o", output.type)
    return [f"    {standin}.struct_size = (uint32_t)sizeof({output.type});",
            f"    {standin}.struct_version = {version};"]


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
        "/* A route whose feature this build compiled out answers NOT_SUPPORTED to everything. */",
        "static int expect_unless_compiled_out(const CNA_Result result, const CNA_Result wanted,",
        "                                      const char* const route, const char* const contract)",
        "{",
        "    return result == CNA_RESULT_NOT_SUPPORTED ? 0 : expect(result, wanted, route, contract);",
        "}",
        "",
        "/* A route that checks its arguments first may refuse the zeroed stand-ins, not the handle. */",
        "static int expect_unless_arguments_refused(const CNA_Result result, const CNA_Result wanted,",
        "                                           const char* const route,",
        "                                           const char* const contract)",
        "{",
        "    return result == CNA_RESULT_INVALID_ARGUMENT ? 0 : expect(result, wanted, route, contract);",
        "}",
        "",
        "/* A refused route leaves a handle output invalid rather than as the caller left it. */",
        "static int expect_cleared(const CNA_Handle output, const char* const route)",
        "{",
        "    if (output == CNA_INVALID_HANDLE) {",
        "        return 0;",
        "    }",
        '    printf("%s: a refused call left its handle output set\\n", route);',
        "    return 1;",
        "}",
        "",
        "static int null_outputs_are_refused(void)",
        "{",
        "    int failures = 0;",
    ]
    for r in routes:
        if r.kind == "result":
            for output in (o for o in r.outputs if o.name not in r.optional):
                lines.append(_expect(f"{r.name}({_arguments(r, **{output.name: 'NULL'})})",
                                     "CNA_RESULT_INVALID_ARGUMENT", r.name,
                                     f'"a null {output.name}"', "feature" in r.group))
        elif r.kind in ("value", "get", "size"):
            output = r.outputs[0].name
            lines.append(_expect(f"{r.name}({_arguments(r, **{output: 'NULL'})})",
                                 "CNA_RESULT_INVALID_ARGUMENT", r.name, f'"a null {output}"',
                                 "feature" in r.group))
            if r.versioned:
                lines += _versioned_header(r, "0U")
                lines.append(_expect(f"{r.name}({_arguments(r)})", "CNA_RESULT_INVALID_ARGUMENT",
                                     r.name, f'"a {output} of version 0"', "feature" in r.group))
        elif r.kind == "copy":
            destination, capacity, out_bytes = (p.name for p in r.outputs)
            lines.append(_expect(f"{r.name}({_arguments(r, **{destination: 'NULL', capacity: '1U'})})",
                                 "CNA_RESULT_INVALID_ARGUMENT", r.name,
                                 '"a null destination with a capacity"', "feature" in r.group))
            lines.append(_expect(f"{r.name}({_arguments(r, **{out_bytes: 'NULL'})})",
                                 "CNA_RESULT_INVALID_ARGUMENT", r.name, '"a null out_bytes"',
                                 "feature" in r.group))
        for param in r.inputs:
            if param.pointer:  # every other argument valid, so only the null input is wrong
                if r.versioned:
                    lines += _versioned_header(r, "1U")
                lines.append(_expect(f"{r.name}({_arguments(r)})", "CNA_RESULT_INVALID_ARGUMENT",
                                     r.name, f'"a null {param.name}"', "feature" in r.group))
    lines += ["    return failures;", "}", "",
              "/* `handle` goes to every route but the curve routes, which get `curve_routes_handle`. */",
              "static int handles_are_refused(const CNA_Handle handle,",
              "                               const CNA_Handle curve_routes_handle,",
              "                               const char* const what)",
              "{", "    int failures = 0;"]
    for r in routes:
        if _takes_handle(r):
            handle = "curve_routes_handle" if r.handle.type == "CNA_CurveHandle" else "handle"
            cleared = [o for o in r.outputs
                       if r.kind == "result" and r.types.kind_of(o.type) == "handle"]
            lines += [f"    {_standin('o', o.type)} = UINT64_C(0xDEAD);" for o in cleared]
            if r.versioned:
                lines += _versioned_header(r, "1U")
            check = [_expect(f"{r.name}({_arguments(r, handle)})",
                             "CNA_RESULT_INVALID_HANDLE", r.name, "what", "feature" in r.group,
                             bool(r.refusals or r.validations))]
            check += [f'    failures += expect_cleared({_standin("o", o.type)}, "{r.name}");'
                      for o in cleared]
            if r.group.get("accepts_null_handle"):
                # The lookup reads the null handle as "none", so only a real handle is refused.
                check = [f"    if ({handle} != CNA_INVALID_HANDLE) {{"] + [
                    "    " + line for line in check] + ["    }"]
            lines += check
    lines += ["    return failures;", "}", "",
              "static int text_protocol_holds(void)", "{",
              "    int failures = 0;", "    uint64_t size = 0U;", "    CNA_Result size_result;"]
    copies = {(r.group.get("lookup"), r.expression, tuple(r.inputs)): r
              for r in routes if r.kind == "copy" and not r.has_lookup}
    for r in routes:
        pair = copies.get((r.group.get("lookup"), r.expression, tuple(r.inputs)))
        if (r.kind != "size" or r.has_lookup or pair is None or r.refusals or pair.refusals
                or r.validations or pair.validations):
            continue
        size_args = _arguments(r, **{r.outputs[0].name: "&size"})
        capacity = pair.outputs[1].name
        short = _arguments(pair, **{capacity: "size - 1U"})
        exact = _arguments(pair, **{capacity: "size"})
        exact_args = _arguments(pair, **{capacity: "(uint64_t)sizeof(text)"})
        lines += [
            # A text that does not exist for zeroed input (no pending input, say) must be refused
            # alike by both halves; one that does must keep the protocol.
            f"    size_result = {r.name}({size_args});",
            "    if (size_result != CNA_RESULT_SUCCESS) {",
            "    " + _expect(f"{pair.name}({exact_args})", "size_result", pair.name,
                             '"the refusal its size half gave"'),
            "    }",
            "    if (size_result == CNA_RESULT_SUCCESS && size > 0U && size <= sizeof(text)) {",
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
    text = "\n".join(lines)
    for helper in ("expect_unless_compiled_out", "expect_unless_arguments_refused", "expect_cleared"):
        if text.count(f"{helper}(") == 1:  # only its definition: leave it out
            text = re.sub(r"/\*[^\n]*\*/\nstatic int " + helper + r"\(.*?\n\}\n\n", "", text,
                          flags=re.S)
    lines = text.split("\n")
    spelled = {_standin("v", p.type)[2:]: p.type for route in routes for p in route.params}
    used = sorted(set(re.findall(r"\b([vo])_(\w+)", "\n".join(lines))))
    standins = [f"static {spelled[name]} {kind}_{name};" for kind, name in used if name in spelled]
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
