# Offline persistence qualification — GS-AUDIT-P4

The contract is **OS-CACHE-DURABLE**, providing ordinary application persistence on supported local filesystems. It is not a database-grade crash or power-loss promise. This follows the checked replacement implementation, existing offline-store API, and `plans/plan_net.md` Task 4.7. No fsync was added to progress/profile writes. Reads remain tolerant of missing/corrupt progress, as required by that plan; writes now refuse unreadable or structurally invalid existing histories rather than destroy recovery evidence.

## Exact boundaries

`LocalGamerServicesStore.cpp`: create parent → stable sibling `LocalStoreLock` → read/modify → serialize JSON → open sibling `<target>.tmp` with truncation → checked stream write → checked userspace flush → checked close → filesystem rename → acknowledge. Temp and target share a directory/filesystem. The old target is never opened for truncation. Stream/replace failures throw; a created temp is removed best effort. Writer locks cover the complete operation. Public offline rating setters roll back their in-memory rating on failure; failed achievement Begin does not signal successful completion.

Phase 4 additionally rejects updates when an existing target cannot be parsed or lacks the expected object/array shape. Existing tolerant per-record numeric/unknown-type salvage is retained; this is not general repair or restoration of previously rounded values. Corrupt files need an operator/user to back up and deliberately repair/remove them before saving resumes. No automatic backup, migration or recovery database was invented.

`SignedInGamer::AwardAchievement` formerly checked for an earned key before taking the store lock. Competing calls could both pass this check and overwrite the first timestamp inside the serialized save. Its save now requests insert-once under the lock and returns whether it wrote. Losing duplicates return without another notification. The generic internal save's explicit overwrite mode remains for existing internal callers/fixtures; public awards use insert-once. Completion time offline comes from the local clock; online comes from the server clock. Neither public award accepts a timestamp or progress value.

`LocalProfiles.cpp`: stable lock + preserved corrupt profile policy → serialize → unique sibling temp → write/flush checks → stream destructor closes → checked rename → boolean status. This close is not separately checked; no close-only failure was reproduced. Unlike progress, best-effort profile creation can retain an in-memory fallback without claiming durable success. Avatar metadata uses this profile path. Catalog installation uses staging and version locks; downloaded assets are immutable/hash checked. CredentialStore is distinct: native flush/fsync plus replacement and restrictive ownership; it does not establish a parent-directory/power-loss guarantee for every platform. SQLite online awards/scores use FULL durable transactions, not these JSON files.

## Questions resolved

| Question | Evidence / result |
|---|---|
| Success before later reads see data? | No in executed Linux cases: success follows close/rename; fresh objects/process readers see complete replacement. An already-open reader can legitimately retain old bytes. |
| Partial/zero-length target after application failure? | An ordinary pre-rename failure leaves old target; no direct-truncate fallback. Constructed interrupted temp state is ignored. OS crash/power loss can lose data/metadata because no file/directory sync is promised. |
| Atomic replacement? | Linux local POSIX replacement runtime/source verified; macOS POSIX source verified; Windows library/Win32 source verified, runtime still required. Not established on network filesystems. |
| Same filesystem? | Sibling temp path by construction, subject to externally modified/symlinked paths outside the cooperating-writer contract. |
| fsync / FlushFileBuffers? | Needed for stronger guarantees; not required by this ordinary persistence contract and not added. Stream flush drains userspace into the OS cache. |
| Containing directory synced? | No for progress/profile stores. Therefore renamed metadata is not promised stable across power loss. |
| Replacement failure? | Throw/false as appropriate; old target retained; owned temp cleanup best effort. Permissions/sharing failures do not trigger a destructive fallback. |
| Old/new recovery? | Application restart reads the named target; completed rename gives new state, interruption before rename gives old state. No promise for OS crash/reordering. |
| Orphan temp files? | Possible after abrupt process death/cleanup failure. Readers never promote them. Fixed progress temp is overwritten by next locked save; unique profile/cache temps may remain and need manual cleanup. |
| Startup recovery? | Ignore temp names, read only target. Preserve malformed target on updates. No automatic selection between uncommitted temp and target. |

## Platform evidence

| Platform | Replacement / encoding / locks | Qualification |
|---|---|---|
| Linux | filesystem paths use UTF-8 helpers; sibling rename; flock on stable O_NOFOLLOW regular lock file; create_directories tolerates cooperating creation races; open old readers retain old inode | **RUNTIME-VERIFIED** local filesystem, failures, restart/fresh objects and cooperating thread/process writers |
| Windows | progress uses native filesystem paths and CreateFileW/LockFileEx stable locks; Microsoft STL rename uses MoveFileExW with REPLACE_EXISTING. Existing/open targets can fail because sharing, ACLs or read-only attributes prohibit replacement. Profile unique-temp construction uses narrow path strings and needs non-ASCII qualification. No fallback truncation or broad flush added | **SOURCE-VERIFIED**, **PLATFORM QUALIFICATION REQUIRED** for supported compiler/STL, NTFS, non-ASCII paths, sharing, simultaneous readers/writers and failure propagation |
| macOS | POSIX rename/flock branches; directory metadata and physical-media sync are distinct from stream flush; ordinary local-filesystem contract applies | **SOURCE-VERIFIED**, **PLATFORM QUALIFICATION REQUIRED** on APFS and actual supported build |

[Linux rename](https://man7.org/linux/man-pages/man2/rename.2.html) specifies atomic existing-file replacement and preservation on failure. [Linux fsync](https://man7.org/linux/man-pages/man2/fsync.2.html) distinguishes file sync from directory metadata sync. [Microsoft STL implementation](https://github.com/microsoft/STL/blob/main/stl/src/filesystem.cpp) and [Win32 replacement documentation](https://learn.microsoft.com/en-us/windows/win32/fileio/moving-and-replacing-files) disprove a blanket Windows “rename cannot replace” claim; they do not qualify CNA on Windows. [Apple rename](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/rename.2.html) and [Apple fsync](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fsync.2.html) support the POSIX/media distinction. No storage medium was crash/power-loss tested.

## Focused regressions

- `OfflineDurabilityTest.CorruptProgressIsReadableAsEmptyButNeverOverwrittenByAnUpdate`: malformed JSON, null, missing array and wrong array type, both store families; byte-for-byte preservation and no temp creation. **Failed before fix.**
- `OfflineDurabilityTest.InterruptedTemporaryFileIsIgnoredAndNextUpdateReplacesIt`: constructs interrupted sibling temp bytes; old timestamp/rating remain readable, next save retains history and clears the fixed temp. **Passed before/after**; this is a failure-state probe, not a destructive crash simulation.
- `OfflineConcurrencyTest.CompetingDuplicateAwardsPreserveTheFirstCompletionAndOnlyOneSuccess`: eight callers all read unearned before a barrier; one write wins, first timestamp survives. **Failed before insert-once** (8 successful updates), passes afterward. The red adapter exposes the new internal option while retaining the old overwrite logic; it is not an unmodified baseline binary.
- Existing Phase 2 disk-full/open/parent/replacement failure and Phase 3 thread/process history regressions rerun. Final counts/repeats are in the Phase 4 handoff/evidence.

Offline streams and column-only durability remain **ACCEPTED DESIGN / XNA COMPATIBILITY LIMITATION**: scalar columns are persisted on a rating assignment (even assigning its current value); independent column edits do not acknowledge a save. Offline stream columns are unsupported. A title needing their service behavior must use online gameplay commits or its own game storage. No general cloud save or transactional storage subsystem was added.
