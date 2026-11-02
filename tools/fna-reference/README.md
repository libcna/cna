# FNA reference probe

This developer tool runs FNA itself and emits JSON for comparison with CNA. It is separate
from language bindings. The project targets .NET 8 and references the output of FNA.Core.csproj;
Mono/xbuild and the former fixed absolute DLL path are no longer used.

## Setup on a new machine

Install the .NET 8 SDK, Git and Python 3.11 or later. Provide an existing FNA checkout outside
CNA with its pinned managed submodule sources initialized (SDL2-CS, SDL3-CS, FAudio, Theorafile
and dav1dfile). Provision dependencies according to their own contribution policies. This recipe
never clones, initializes submodules or edits FNA source. Its build outputs stay in the checkout's
ignored build directories. Choose a reviewed, full FNA commit ID rather than a moving branch.

```sh
export FNA_SOURCE_DIR=/path/to/FNA
export FNA_EXPECTED_REVISION=<reviewed-40-character-commit-id>
python3 tools/fna-reference/run-reference.py --output-dir /path/to/fresh/evidence
```

The recipe verifies a clean tracked source tree and its revision, rebuilds FNA.Core.csproj,
gets its actual TargetPath from MSBuild, hashes FNA.dll, builds this probe, and runs the values
probe twice. It compares NonRenderingApis, PackedVector, Viewport, Vector2Edges and
MatrixPerspectiveEdges separately at **zero**
tolerance with scripts/compare-fna-reference.py. It exits nonzero if any build, run, provenance
check or comparison fails. Both dumps remain in the requested output directory as
values-1.json and values-2.json. No window or GraphicsDevice is needed for these five probes.
No timestamps or random seeds enter their metadata.

By default, existing outputs and all output directories inside the CNA checkout are refused.
`--update` explicitly allows replacing the recipe's two named outputs and writing inside CNA;
it does not automatically copy anything into tests/fixtures. Output-file symlinks are always
refused; directory symlinks are resolved before checking the repository boundary. Use a fresh
external directory for ordinary evidence. Committed goldens should only change after reviewing
fresh reference output and explicitly choosing to update them.

For manual builds/runs, all paths are configurable:

```sh
dotnet build "$FNA_SOURCE_DIR/FNA.Core.csproj" --configuration Debug -m:4
export FNA_ASSEMBLY_PATH="$(dotnet msbuild "$FNA_SOURCE_DIR/FNA.Core.csproj" -getProperty:TargetPath -p:Configuration=Debug)"
export FNA_EXPECTED_SHA256="$(sha256sum "$FNA_ASSEMBLY_PATH" | cut -d ' ' -f 1)"
dotnet build tools/fna-reference/FnaReference.csproj -m:4 \
  -p:FnaAssemblyPath="$FNA_ASSEMBLY_PATH"
dotnet tools/fna-reference/bin/Debug/net8.0/FnaReference.dll /path/to/fresh/values.json
```

`FnaAssemblyPath` is an MSBuild property; when absent, it uses `FNA_ASSEMBLY_PATH`.
Unset/missing paths are build errors. Runtime additionally requires FNA_SOURCE_DIR,
FNA_EXPECTED_REVISION, FNA_ASSEMBLY_PATH and FNA_EXPECTED_SHA256. It checks the checkout's
actual Git revision, tracked cleanliness, configured DLL hash and **loaded** DLL hash. A stale
copied DLL cannot silently substitute for the configured one. The recipe computes the assembly
pin only after building the selected source; manual users must retain the same provenance chain.
An explicit output filename is required. Direct invocation writes that filename; use the recipe
for the overwrite protections.

## Metadata and comparison

Every mode emits `metadata.reference` with this versioned identity:

```json
{
  "schemaVersion": 1,
  "sourceRevision": "<40 lowercase hex digits>",
  "assemblySha256": "<64 lowercase hex digits>",
  "assemblyVersion": "26.5.0.0",
  "nativeLibraries": {
    "FNA3D": {"status": "not_loaded"},
    "SDL3": {"status": "not_loaded"}
  }
}
```

The assembly version above is illustrative, not a pin. Loaded native entries instead contain
`status: "loaded"`, `sha256` and an integer `version` returned by their version exports.
Pure managed runs record not_loaded rather than suggesting that a native library was tested.
Diagnostic source/assembly/native paths live alongside the reference identity in metadata;
paths may differ across machines, while the identity must match exactly.

The first comparator input is the FNA reference and must carry valid metadata.reference.
The second input is a candidate: ordinary CNA dumps are accepted without FNA metadata, because
CNA never loads that assembly. If the candidate supplies metadata.reference (for an FNA-vs-FNA
determinism comparison), it must validate and match the first input exactly, independently of
category selection and tolerance. A present null or malformed reference is rejected, not treated
as absent. Never invent FNA metadata for old reference goldens: regenerate them with the pinned
reference. Legacy dumps without provenance are rejected in the first input position.

Candidate metadata, when present, must be an object. Its optional metadata.producer object must
contain exactly `tool` (a nonempty string) and `cnaRevision` (40 lowercase hexadecimal digits),
for example `{"tool":"cna_reference_dump","cnaRevision":"<full-CNA-commit>"}`. It describes the
candidate independently of the FNA oracle and is printed in the PASS/FAIL comparison summary.
Malformed producer metadata is rejected. The current native CNA dump has no producer metadata;
adding an accurate build revision needs separate build integration. It remains a valid candidate.
`--compare-to /path/to/cna.json` compares all five recipe categories with the ordinary numeric
tolerance. It fails on retained XNA-over-FNA differences; it is not the disposition audit. See
[bounded edge coverage](../../docs/fna-reference-harness.md#bounded-value-and-exception-edges-mth-conf-003)
for the raw zero-tolerance comparison, exact retained-difference audit and real-dump tests.

The default categories are NonRenderingApis (graphics enums/state presets), PackedVector
(17 types) and Viewport (Project/Unproject). Integer pairs compare exactly; finite floats use
per-field absolute tolerance (default 1e-4 for CNA comparison); signed zero is equivalent.
Nonfinite values, duplicate keys, missing/empty categories and invalid JSON fail closed.
Extra CNA object keys are permitted. See the [full comparison contract](../../docs/fna-reference-harness.md#comparator-meaning-and-limits).

```sh
python3 -B scripts/test_compare_fna_reference.py -v
python3 -B tools/fna-reference/test_run_reference.py -v
```

These CPU-only tests need no FNA installation. Synthetic metadata in unit tests is test data,
not retroactive provenance for the committed effect fixtures.

## Native effect modes (integration owner only)

All three modes need SDL3/FNA3D and a private display, including reflection, which creates a
native device. Build/provision shared native dependencies for the selected FNA checkout and
review their hashes. A matching filename or version alone does not establish compatibility.
The probe checks SHA-256 pins, calls FNA3D_LinkedVersion and SDL_GetVersion, verifies the
FNA3D version against the managed assembly, and uses Linux dladdr to hash the library actually
owning each resolved version export. A strict resolver pins direct P/Invokes in both assemblies;
/proc/self/maps checks refuse other mapped SDL/FNA3D identities, including transitive loads.
FNA.dll.config is excluded from reference copying and refused if left in the harness output,
because FNA's own resolver would otherwise compete with the explicit pins.

This native provenance implementation currently supports **Linux only**; other platforms fail
explicitly for effect modes. Native load, ABI compatibility and graphics behavior still require
an actual run. Pure managed mode has no native dependency. FNA3D must use SDL3 for these probes.

```sh
export FNA3D_LIBRARY_PATH=/path/to/reviewed/libFNA3D.so
export FNA3D_EXPECTED_SHA256=<reviewed-sha256>
export SDL3_LIBRARY_PATH=/path/to/reviewed/libSDL3.so
export SDL3_EXPECTED_SHA256=<reviewed-sha256>
# Set LD_LIBRARY_PATH as needed for the native libraries' transitive dependencies.
# Retain FNA_SOURCE_DIR/FNA_EXPECTED_REVISION from setup.
for mode in effects effect-states effect-pixels; do
  tools/platform/run_gpu_tests_private.sh --exec \
    python3 tools/fna-reference/run-reference.py --mode "$mode" \
      --effects-dir modules/renderers/fna3d/effects \
      --output-dir "/path/to/fresh/evidence/$mode"
done
```

`--effects` records FNA's Effect parameter/technique graph via its internal parser mapping.
`--effect-states` applies passes and records public device states from fixed defaults.
`--effect-pixels` draws to an 8x8 target with flat textures/identity transforms and records
changed-pixel counts plus interior RGBA samples. The recipe's two-run check is exact; the
specialized CNA pixel test separately permits its established three-level per-channel variance.

## Verification boundary (MTH-CONF-002)

On 2026-11-02, after the owner provisioned the five pinned managed submodules, the recipe built
FNA revision 00498b0b81425b14bf8da23e68655215b9b5daa7 and the full harness with SDK 8.0.425.
FNA reports assembly version 26.10.0.0; the built and loaded DLL SHA-256 was
`f973ec1edd32290f19750d6d3d26fba23ab1a813fedd71f1ea026edf03c9647c`.
Both value runs produced byte-identical dumps, and all three categories passed the comparator
at zero tolerance. FNA3D and SDL3 were not loaded; the uninitialized FNA3D source submodule was
not needed for the managed build. Five runtime negative checks rejected missing settings/files,
a wrong revision/hash and a configured-versus-loaded assembly mismatch. Reference-side missing
provenance and candidate-side altered provenance are rejected. All 49 comparator and 5 recipe
tests passed, including candidate dumps without FNA metadata and producer validation.

The builds reported upstream CA2255 (module initializer) and existing probe SYSLIB0050
(FormatterServices) warnings, with no errors. No dependency source was edited and no goldens
were replaced. Native effect runs remain integration-owner work under the private compositor.
This proves reference repeatability on this host, not fresh CNA/FNA parity or native compatibility.
