---
name: port-fna-file
description: Per-file checklist and minimum requirements for porting a .cs file from FNA to CNA (SPDX headers, CNAEXT include, line-by-line FNA verification, GetTypeName override, tests). Use when porting a new FNA/XNA source file to CNA or reviewing whether a ported file is complete.
---

Every `.cs` file ported from FNA to CNA **must be complete** — not partial. "Make and forget" means the file is
done in one pass. Do not skip any checklist item and come back later.

The full per-file checklist is in:

```text
CHECKLIST.md
```

Use it for every file. The minimum requirements are:

- `// SPDX-License-Identifier: MS-PL` at the top of both `.hpp` **and** `.cpp`.
- `#include "CNA/CNAHelper.hpp"` in `.hpp` if `CNAEXT` is used anywhere in that file.
- Every method body verified **line-by-line** against the FNA equivalent.
- Every intentional deviation from FNA logic documented with a `//` comment in the source.
- Concrete classes that inherit `System::Object` **must** override `GetTypeName()` with `CNAEXT`:
  ```cpp
  CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
  ```
  The return value is the fully-qualified .NET name, e.g. `"Microsoft.Xna.Framework.Game"`.
- Tests: every public method, operator, and constant covered. Out-ref overloads tested separately.
  See CHECKLIST.md for the complete test requirements.

The table of known acceptable C++ deviations from FNA/XNA (e.g. `GetHashCode()` returning `std::size_t`,
`ref`/`out` → value-ref pairs, null guards omitted for C++ references) is maintained in `CHECKLIST.md`.
