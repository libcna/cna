# XNA 4.0 runtime member coverage

> **Generated signature-coverage report.**
> [Machine-readable findings](xna-4-runtime-member-coverage.json) are produced by
> `python3 tools/audit_xna_runtime_surface.py --write-reports` from the local Microsoft
> reference files and current CNA headers. Keep headers unchanged during its Clang pass.

Microsoft runtime XML defines the documented census. Matching Microsoft DLL metadata supplies
return types, static ownership, visibility, accessor shape, and enum values. Clang parses CNA
public headers. Content Pipeline XML is excluded. API representation does not establish behavior.

Runtime public types: **331 / 331** (100.00%)

| Member kind | Represented | Documented | Coverage |
| --- | ---: | ---: | ---: |
| Constructors | 253 | 253 | 100.00% |
| Methods | 1518 | 1518 | 100.00% |
| Properties | 1040 | 1040 | 100.00% |
| Fields | 753 | 753 | 100.00% |
| Events | 63 | 63 | 100.00% |
| Operators (subset) | 145 | 145 | 100.00% |
| Indexers (subset) | 29 | 29 | 100.00% |
| Enum Values (subset) | 661 | 661 | 100.00% |

Strict documented members: **3627 / 3627** (100.00%)
C++-applicable members: **3627 / 3627** (100.00%)

## Classifications

- EXACT_EQUIVALENT: 1746
- SEMANTIC_EQUIVALENT: 1823
- HOST_LANGUAGE_SUBSTITUTION: 58
- MISSING: 0
- NOT_APPLICABLE: 0
- NEEDS_REVIEW: 0

`EXACT_EQUIVALENT`, `SEMANTIC_EQUIVALENT`, and `HOST_LANGUAGE_SUBSTITUTION` count as represented.
Only `NOT_APPLICABLE` is removed from the C++-applicable denominator.
`NEEDS_REVIEW` is excluded from both numerators. Every exception requires a per-entry reason.

These classifications describe the audit's declaration/signature matching, including explicit
C++ normalization and host substitutions. EXACT_EQUIVALENT here does not assert that a method's
implementation, tests, exceptions or platform behaviour match XNA. The JSON findings explicitly
mark behaviour as unassessed. Subset rows overlap their parent categories and must not be
added to the documented-member denominator.

## Gap review

Tier A gaps remaining: **0**; Tier B: **0**; Tier C: **0**.
No documented runtime member is unrepresented, and the denominator is unchanged: the
same ten reference assemblies, the same corpus hashes, no `NOT_APPLICABLE` exemption
and no ignore list. This measures representation of the documented member surface
only. Behavior, exception detail, renderer results and historical online-service
availability are separate measures, and none of them is claimed here.

## Remaining missing members

None.

## Entries needing review

None.

## Methodology

The reference set is the ten runtime XML files from the XNA 4.0 SDK; the build-time
Content Pipeline XML is excluded. The XML entry is the unit of measurement, including
enum fields and explicit interface members. CLR generic arity, overload parameters,
ref/out markers, static ownership, return/type metadata, property accessors, and enum
numeric values are retained. The CNA model uses public/protected Clang AST declarations.
C++ `const T&` maps an input value, mutable `T&` maps CLR byref, pointer and collection
shapes are considered only through explicit normalization. Property accessor pairs use
CNA's `getXProperty` / `setXProperty` convention. Ambiguity remains `NEEDS_REVIEW`.
Eight explicit `GetEnumerator` XML records are absent from the corresponding
Microsoft DLL metadata; they remain in the denominator and have per-entry notes.
Behavior, exceptions, event delivery, renderer results, and historical online-service
availability require separate validation.
The machine-readable JSON contains each reference entry, match, and classification.

## Reproduction limits

The type census scans namespace/owner-aware declarations. The member pass uses matching
Microsoft DLL metadata and Clang public/protected AST declarations under `CNA_RENDERER_HEADLESS`,
with module and sibling Sharp Runtime include roots. It runs up to four Clang workers; it is
not a build/link test of every renderer configuration, a consumer package, or method bodies.
The retained XML/DLL hashes identify the reference inputs; availability of those local files,
Clang, Mono and mcs is required for a fresh full audit.

The full CLI audit exits nonzero for missing types, missing or unreviewed members, or AST errors.
`--types-only` checks type presence without running the member audit.
Inspect the member classifications, AST errors and JSON findings as well as the process status.
The audit does not certify the per-method unit-test completion required by CHECKLIST.md.
Current behaviour/qualification gaps remain in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md) and subsystem guides.
