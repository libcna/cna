# XNA packed-half conversion evidence (MTH-CONF-003)

Local inspection on 2026-11-02 used the original Microsoft XNA 4.0 Windows assembly:

- File: `Microsoft.Xna.Framework.dll`.
- Version: `4.0.0.0`; public key token: `842cf8be1de50553`.
- SHA-256: `38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130`.

The repository contains original prose summaries and regression inputs/outputs only. Microsoft
assembly disassembly is local evidence outside the repository and must not be redistributed here.

## Inspection summary

The private type `Microsoft.Xna.Framework.Graphics.PackedVector.HalfUtils` implements the shared
conversion for HalfSingle, HalfVector2 and HalfVector4 in this assembly.

- `Unpack(uint16)`, offsets `IL_005f`–`IL_008a`: every nonzero exponent is converted to a finite
  Single exponent, including exponent 31. Codes `7c00`, `7e00`, `7fff` expand to `65536`, `98304`,
  `131008` with the stored sign. No infinity/NaN special case is applied to exponent 31.
- `Pack(float32)`, offsets `IL_001b`–`IL_002c`: absolute Single bit patterns above `47ffefff`,
  including infinity and NaN, saturate to the signed maximum code `7fff`. Offsets
  `IL_002e`–`IL_0072` handle subnormal conversion and ties-to-even rounding; `IL_0074`–`IL_008e`
  handle normal conversion and rounding. Extended exponent-31 finite values remain representable.
- `HalfSingle.ToSingle`, offset `IL_0006`, uses Unpack; its interface ToVector4 uses ToSingle.
  `HalfVector2.ToVector2`, offsets `IL_0009`–`IL_001f`, and `HalfVector4.ToVector4`, offsets
  `IL_0009`–`IL_004b`, expand each lane through Unpack. Constructors and IPackedVector.PackFromVector4
  use Pack directly or through PackHelper; PackedValue setters preserve the supplied integer.

CNA's three `XnaHalfConversionTest` regressions independently cover signed zero, subnormal ties,
extended finite values, saturation boundaries, and signed NaN/infinity inputs through helper and
public API paths. All three failed before the repair. FNA shares finite unpacking but uses
IEEE-style overflow and NaN/infinity packing. The audit retains only exact observed differences.

## Regenerate locally

Use your own copy of the identified assembly and an output directory outside this repository:

```bash
XNA_ASSEMBLY="/path/to/your/Microsoft.Xna.Framework.dll"
LOCAL_EVIDENCE="/path/to/local/evidence/outside/cna"
sha256sum "$XNA_ASSEMBLY"
ikdasm "$XNA_ASSEMBLY" > "$LOCAL_EVIDENCE/framework.il"
```

Do not commit the resulting disassembly. Inspect the named types, methods and offsets locally.

`XnaHalfProbe.cs` is CNA-owned code calling the public XNA API. Its 57 records cover all reviewed
half inputs and can be compiled against the maintainer's assembly:

```bash
mcs -r:"$XNA_ASSEMBLY" -out:"$LOCAL_EVIDENCE/XnaHalfProbe.exe" tools/fna-reference/xna-half/XnaHalfProbe.cs
MONO_PATH="$(dirname "$XNA_ASSEMBLY")" \
  env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  mono "$LOCAL_EVIDENCE/XnaHalfProbe.exe" > "$LOCAL_EVIDENCE/xna-half.tsv" 2> "$LOCAL_EVIDENCE/xna-half-identity.log"
```

Compilation succeeded on the inspected host, but the original mixed-mode assembly could not
initialize under Mono: unsupported native code caused an exception before any measurement.
No successful original-XNA runtime measurement is claimed. This decision rests on local inspection
of the identified shipped assembly; the audit and evidence tests do not load disassembly files.
