# Audit: include/CNA/Internal/Media/VideoDecoder.hpp

## Metadata
- Source file: `include/CNA/Internal/Media/VideoDecoder.hpp`
- Audit status: AUDITED (full read, 166 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ header
- XNA/FNA relevance: N/A -- NOXNA internal FFmpeg wrapper behind `VideoPlayer`
- Main related tests: not independently located in this pass

## Purpose
Declares the internal FFmpeg-based decoder owning all decode state for one open video file: codec
contexts, resampler, pending-packet retry state, and stream-metadata accessors.

## Executive Verdict
Healthy -- this header (and its paired .cpp) show an unusually mature audit history: numerous in-line
comments cite specific prior external-code-review findings (plans/plan_media.md MEDIA-38/39/40/94/128/130/131/
146/147/154/155/158/159/160/162/163/164/167/169) with the exact reasoning for each fix, not just a task ID.
One new MEDIUM-severity finding from this pass -- see the paired `.cpp`'s report.

## Checklist Results

### Documentation quality: exceptional
`HasAudio()`'s doc comment (lines 45-58) is a genuinely rare example of documentation that explains not just
what the method does but the full history of why its definition is exactly `audioCtx_ != nullptr &&
swrCtx_ != nullptr` rather than checking `audioCtx_` alone, including which prior fix superseded which. This
is the right way to preserve hard-won bug-fix context for future maintainers rather than letting it evaporate
once the PR closes.

### API contract clarity
`SetAudioStream()`/`SetVideoStream()`'s bool-return contract (true only if a genuine switch happened) is
documented precisely, including why the caller (`VideoPlayer`) actually needs this distinction (skipping
destructive downstream reconfiguration for a no-op switch).

## Detailed Findings
None in this header -- see the paired `.cpp` for one MEDIUM-severity finding.

## Cross-File Observations
`havePendingVideoPacket_`'s promotion from a function-local to a class-member flag (MEDIA-146, documented
directly in the member's own comment, lines 143-149) is a good example of a subtle stateful-retry bug that
would be very easy to silently reintroduce during a future refactor without this comment.

## Missing or Weak Tests
Not independently located in this pass; given the sheer number of previously-found subtle bugs in this
exact class, dedicated regression tests for each (EAGAIN retry survives across calls, seek clears pending
audio, track-switch transactionality) would be high-value if not already present.

## Positive Findings
The best-documented bug-fix history of any file audited in this shard so far -- a strong positive signal
for how seriously this subsystem's prior review findings were tracked and fixed.

## Final Assessment
No issues in this header; see the paired `.cpp` for one MEDIUM-severity finding (stale cached dimensions
used for frame conversion).
