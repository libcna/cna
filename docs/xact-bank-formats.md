# XACT bank formats

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

## Wave-bank preambles

XWB content41 has no separate header-version word. Content42 through46 includes one
u32 immediately after the content version, before the five segment records. The shared
resident/streaming reader uses that boundary; a no-header control must be authored as41.

| Content | Header bytes | Segment table starts at | Header-version word |
|---|---:|---:|---|
| 41 | 48 | 8 | Absent, reported as0 |
| 42–46 | 52 | 12 | Present |

Sixteen literal LE native controls cover content41 and every pair of content42–46 with
header versions42,43,44. Each is one non-compact, resident, mono PCM16 entry at22050Hz,
with four data bytes. Public GetWaveProperties derives duration2 from play length4;
the native entry locates those bytes at168 for41 and172 otherwise. This qualifies header
placement and metadata, not decoded or rendered native audio. CNA additionally checks
the same resident inputs and32 streaming-header controls at file offsets0/2048, preserving
absolute wave offsets and retaining no PCM body in the streaming metadata prefix.

The native probe uses public CreateEngine, CreateInMemoryWaveBank, property queries,
DoWork, wave-bank destruction and Release, without Initialize, voices, a device or an
audio thread. Its first LSan run exposed two mutexes lost when the pinned Release calls
ShutDown: that function zeroes sbLock/wbLock before Release destroys them. The harness
captures and destroys those orphaned handles after Release; reference code is unchanged,
and the original leak diagnostic is retained. This cleanup is specific to the pinned
revision. With that explicit cleanup, all native ASan/UBSan/LSan checks pass, including
alignment. Internal reference code is instrumented and public entry points come from
the uninstrumented archive.

The change adds no version/header-version whitelist. Existing CNA low-version fixtures
and header-version validation policy are retained. The native controls are valid accepted
format combinations, without claims about historical Microsoft encoder output, compact
or encoded banks, native streaming or playback. Evidence is archived under
`../cnaplan/evidence/rev002/xact-xwb-header/`.
