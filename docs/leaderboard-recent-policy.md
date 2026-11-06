# CNA Recent leaderboard policy (GAP-18)

This is CNA policy approved by the owner on 2026-11-06. No measured Xbox LIVE window
is claimed. XNA 4.0 Refresh LeaderboardIdentity.Create(key,mode), IL_000a–0023,
identifies keys; the Windows LeaderboardReader.Read overloads IL_0000–000a refuse
service access and supply no retention semantics.

The exact keys BestScoreRecent and BestTimeRecent, in every game mode, retain one
row per gamer. Scores prefer larger signed 64-bit ratings; times prefer smaller
ratings. A better **or equal explicit write** replaces rating and columns together
and timestamps the row at successful persistence. A worse active candidate changes
nothing, including timestamp. An expired incumbent cannot block a new candidate,
even one worse than the old best. Merely opening a writer, reading, paging, listing,
restarting, or retrying an already committed service epoch does not refresh it.
Local Rating assignments persist immediately. An EndGame flush of changed offline
columns is an explicit equal-score rewrite and refreshes the timestamp; an unchanged
flush does nothing. Service drafts timestamp at commit; arbitrated rows timestamp
at successful resolution, not at the first report or game start. Trusted admin seeds
replace a row explicitly and receive the administration write time.

This is one retained best row with an expiry, not a history of every submitted score.
Discarded worse candidates do not become fallback rows when the retained best expires.

Use UTC Unix seconds, with integer-second resolution. A row written at T is visible
while now < T + 604800 (7*24*60*60). It is absent exactly at expiry. Read filtering
precedes sorting, global ranking, friend/gamer restriction, pivot centering, totals
and page slicing. Example: expired A=100, live B=90,C=80 gives B rank 1,C rank 2;
restricting to A,C gives only C, still rank 2. BestTimeRecent sorts ascending.
A local reader caches its read snapshot for PageUp/PageDown; create a fresh reader
to observe subsequent expiry. Service paging requests query live data each time.
Values already returned to a caller are snapshots and are not asynchronously erased.

The local machine clock owns offline timestamps; the service clock owns online ones.
The client sends no retention timestamp. Time-zone and DST settings do not enter the
arithmetic. There is no distributed-clock consensus or tamper resistance for offline
files. Backward UTC corrections can extend visibility of retained rows; future
stamps remain until their own T+604800. Forward corrections can expire rows sooner.
Never rewrite timestamps during clock corrections. Pruned data do not reappear after
clock rollback. Clock injection for tests changes only leaderboard time, leaving
credentials, leases, rate limits and arbitration deadlines on their existing clocks.

Local Recent JSON gains recentPolicyVersion=1 and per-row writtenUtcSeconds. Under
the existing file lock, migrate an unversioned row with no timestamp to the single
captured migration time, then atomically persist before returning it. Preserve
existing valid timestamps and all columns/int64 precision. The marker prevents
repeated grace periods. Invalid timestamps, or absent timestamps in version 1, are
unusable rows, not candidates for renewed migration. Preserve unknown future formats
and refuse to rewrite them. If persistence of migration fails, fail the read/update
without returning a newly granted window. Missing/corrupt files retain the existing
tolerant read behavior. Physical pruning occurs on valid Recent reads and writes.

Service schema 26 activates canonical Recent best/direction policy and an expiry
index; existing NOT NULL updated Unix-second timestamps are preserved. They already
supply the required timestamp; no redundant timestamp column is added. Schemas 0–2
had no leaderboard rows. A missing updated column/NULL in an alleged schema 25 DB
is an invalid database, not a supported old row format. No fabricated migration-time
reset of genuine old service scores is permitted. An import without timestamps must
first assign import/migration time through the trusted administration path.
The migration normalizes only the two canonical keys; lifetime/custom catalog policy
is unchanged. Invalid new canonical policies are rejected. Deploying schema 26 needs
the owner's schema rollout decision and a verified backup: an older service refuses
the newer database; rollback requires the backup. Wire envelope v1 and public XNA/C
signatures do not change. Expired service rows are filtered in lists and reads, and
physically deleted on successful title-scoped reads/commits. Pruning is lazy; inactive
titles can retain invisible rows on disk until next access. No background worker.
