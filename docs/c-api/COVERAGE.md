# CNA C API Coverage Matrix

## Coverage rule

The final C API covers every public CNA symbol. This matrix is the authoritative evidence for that
claim. A row identifies the source C++ type/member/constant/event, its C-native mapping, owning C
header, implementation/test locations, capability limitations and status.

The matrix is intentionally empty at Phase B0 because no C header exists. Empty rows mean
**unimplemented**, never implicitly unsupported or complete.

## Source inventory boundary

`CBIND-033` will generate the complete inventory from public framework headers under:

```text
modules/*/include/Microsoft/**
modules/*/include/CNA/**
```

The scanner excludes `CNA/Internal/**`, generated/test-only artifacts and the C API's own headers.
Any header exposed to a normal CNA application that is not matched by this rule must be added to the
generator input deliberately. The review output records the exact source commit and header/member
count.

For each inventory entry, one of these C mappings is mandatory:

| Mapping status | Required evidence |
|---|---|
| Implemented | C declaration, adapter implementation, C-only positive/negative tests and ABI layout/export tests where applicable. |
| Planned | Target C header/function family, required mapping form and blocking predecessor. |
| Native limitation | A callable C API reports the exact existing CNA renderer/platform limitation; it has a test and owner-approved documentation. |

`Not applicable` is not a valid status for a public CNA symbol. C++ syntax differences are solved by
a C mapping, not by dropping the symbol.

## Required row fields

```text
Source header and symbol
Public family
C mapping (POD / handle / function set / callback / count-copy)
C header and declaration(s)
Ownership and thread rule
Error/capability behavior
C-only test(s)
ABI test(s)
Status
```

`CBIND-043` will make the inventory comparison a required build/CI gate. Adding a new public CNA
symbol must add its coverage row and matching C API work in the same task; deleting or changing a
public symbol must update the mapping intentionally.
