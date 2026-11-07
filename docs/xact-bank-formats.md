# XACT sound-bank versions

The sound-bank parser distinguishes the versioned layouts used by local FAudio
`6839b88e304a046ae1a609ff14a085371e02a4e0`. Content43 is XACT3.0; content44,45 and46
use the modern track and effect layouts below.

| Field | Content43 | Content44/45/46 |
|---|---|---|
| Complex sound track metadata | Volume byte + absolute u32 event offset:5 bytes | Same, then u16 filter data + u16 frequency:9 bytes |
| Effect tail after pitch/volume bounds, types4 and6 | One legacy flag byte | Four float frequency/Q bounds + u16 flags:18 bytes |
| Track filter defaults | Disabled0xFF, Q0, frequency0 | Parsed from authored metadata |
| Effect frequency/Q defaults | Four zeros | Parsed from the float tail |

Legacy flags map independently:0x01→volume ADD,0x04→pitch ADD,0x10→pitch NEW_ON_LOOP,
0x20→volume NEW_ON_LOOP,0x40→pitch variation and0x80→volume variation. Bits0x02 and0x08
produce no runtime flags. Modern u16 flags pass through unchanged. Each track seeks its
absolute event offset; the following sound starts after the last track's event block.
The legacy tail must consume only its one flag byte, including before a type6 candidate
table, or subsequent fields and sounds would be misread.

A native probe calls actual public CreateSoundBank and Destroy on259 literal banks:
all256 legacy flag bytes and modern44/45/46 controls. Each bank contains two tracks with
type4/type6 events, a gap between their event blocks, a following simple sound and a cue
referencing that sound. The probe checks mapped flags, filter and effect values, offsets,
candidate weights and cue sound codes. Reference internal code is included unmodified
and instrumented; public functions come from the uninstrumented archive. Real SDL mutexes
and normal allocation are used, without a device, playback thread or PCM.

Native UBSan first reports a misaligned packed read_u16 at header offset19. That diagnostic
is retained; only alignment checking is excluded from the native probe's final run.
ASan, the remaining UBSan checks and LSan pass. Its512 stderr lines are the expected
native diagnostics for ignored legacy bits0x02/0x08. CNA keeps every sanitizer check,
including alignment, enabled. These are little-endian format measurements, not Microsoft
XACT or native playback qualification.

CNA regressions cover the same259 sound/track layouts and inputs, following-sound cue
resolution, modern filter controls and every truncated event-block prefix for both effect
families. Malformed native buffers are not executed. Existing header-validation policy,
other bank formats and byte-zero effect-volume policy are unchanged. Evidence is archived
at `../cnaplan/evidence/rev002/xact-xsb43/`.
