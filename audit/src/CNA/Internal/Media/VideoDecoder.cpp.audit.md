# Audit: src/CNA/Internal/Media/VideoDecoder.cpp

## Metadata
- Source file: `src/CNA/Internal/Media/VideoDecoder.cpp`
- Audit status: AUDITED (full read, 860 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements FFmpeg-based Open/Close/SeekToStart/SetAudioStream/SetVideoStream/NextFrame/DrainAudio, plus
from-scratch BT.601 YUV->RGBA conversion for 8-bit and 10-/12-bit planar chroma-subsampled formats, with a
generic per-pixel fallback for other packed/planar formats.

## Executive Verdict
Needs attention -- one new MEDIUM-severity finding (stale cached video dimensions used to index a decoded
frame's actual buffer, a potential OOB read if a stream's resolution changes mid-decode). Everything else in
this file -- resource lifetime, the EAGAIN retry protocol on both video and audio paths, seek-state cleanup,
transactional track-switching, and error propagation -- is correct and, per the extensive existing commentary,
has already been through multiple rounds of external review with real bugs fixed at each site.

## Checklist Results

### MEDIUM: `ConvertFrameToRGBA()` indexes a decoded frame using stale cached dimensions, not the frame's own
`ConvertFrameToRGBA()` (lines 544-598) and both `yuv_planar8_to_rgba()`/`yuv_planar16_to_rgba()` (lines
413-479) index into `frame_->data[...]` using the class members `width_`/`height_` -- captured once, either
at `Open()` time (line 73-74) or at a later `OpenVideoStreamByIndex()` track switch (line 335-336) -- rather
than the just-decoded `frame_->width`/`frame_->height`. FFmpeg's own AVFrame contract allows a decoded
frame's actual dimensions to differ from the stream's initially-reported dimensions for codecs/containers
that support a mid-stream resolution change (e.g. certain H.264/H.265/VP9/AV1 streams via updated parameter
sets) -- when that happens, `frame_->linesize[i]` reflects the NEW frame's real stride, but the loop bounds
here (`row < height_`, `col < width_`, both the STALE cached values) do not. If the new frame's actual
dimensions are *smaller* than the stale cached ones, the per-row/per-column indexing
(`y[row * yStride + col]`, etc.) walks past the end of that row's actual valid data -- and, once `row`
exceeds the new frame's real height, past the end of the plane's allocation entirely -- a genuine
out-of-bounds heap read of FFmpeg-owned memory. `out.resize(width_ * height_ * 4)` (line 576) also sizes the
destination buffer off the same stale values, so the output buffer itself stays internally consistent with
the (wrong) loop bounds, but does not protect the source-side OOB read.

Likelihood in this project's actual use case (game engine cutscene/video playback, not adaptive/live
streaming) is low -- authored game video content is essentially always encoded at a single fixed resolution
-- which is likely why this specific scenario doesn't appear among the many other subtle bugs this file's
extensive comment history already documents finding and fixing. It is flagged here because nothing in the
file currently guards against it, in contrast to this file's otherwise-exhaustive defensive posture toward
every other FFmpeg edge case.

**Fix shape**: after a successful `avcodec_receive_frame()`, compare `frame_->width`/`frame_->height` against
the cached `width_`/`height_`; on a mismatch, either update the cached values (and resize `out` accordingly)
before conversion, or treat it as an unsupported/error case consistent with this file's existing
"surface it, don't silently misbehave" pattern (matching the standard already applied to every other decode
irregularity in this file, e.g. MEDIA-39/40).

### Resource lifetime: correct on every path re-verified in this pass
`Open()`'s every failure branch correctly unwinds exactly what was allocated so far (codec contexts freed
before `avformat_close_input`, `Close()` called wholesale on the final `frame_`/`pkt_` allocation-failure
check) -- independently re-traced line by line, not merely assumed given the file's good track record.
`Close()` correctly frees in reverse-ish dependency order (packet/frame first, then resampler, then codec
contexts, then format context) and clears `pendingAudio_` (MEDIA-155) and `havePendingVideoPacket_`.

### EAGAIN retry protocol: correct on both the video (`NextFrame()`) and audio (`ProcessAudioPacket()`) paths
Both correctly implement FFmpeg's documented "drain via receive_frame, then resend the same packet" contract:
the video path persists the retained packet across `NextFrame()` calls via the `havePendingVideoPacket_`
member (independently re-verified this really does survive a call boundary, since the flag and `pkt_` are
both class members, not locals); the audio path's `while (needsResend)` loop (lines 766-819) drains via the
inner `avcodec_receive_frame()` loop before re-attempting `avcodec_send_packet()` with the *same* `pkt`
argument, matching the contract exactly.

### `SeekToStart()`: correct fail-closed behavior and correct resampler-rebuild strategy
Correctly leaves all state untouched if `av_seek_frame()` itself fails (rather than assuming the seek
succeeded and clearing state on top of an unmoved read position); correctly discards and rebuilds a fresh
`SwrContext` rather than attempting to drain the old one's internal delay buffer (a fresh resampler has zero
internal delay by construction) -- confirmed this sidesteps the exact class of bug the comment describes
(MEDIA-167's prior bounded-drain-loop approach could exit without confirming zero delay).

### `ProcessAudioPacket()`'s buffer sizing: correct
`swr_alloc_set_opts2()`'s src/dst sample rates are identical (only sample-format conversion, no
resampling), so `swr_convert()`'s output sample count can never exceed the input `numSamples` -- the
`std::vector<float> buf(numSamples * channels_)` allocation is therefore always sufficient capacity for the
`numSamples`-per-channel figure passed as `swr_convert()`'s output-capacity argument; the post-call
`buf.resize(converted * channels_)` correctly trims to the actual (possibly smaller) sample count produced.

## Detailed Findings

1. **[MEDIUM] Stale cached `width_`/`height_` used to index a decoded frame's actual buffer in
   `ConvertFrameToRGBA()`/`yuv_planarN_to_rgba()`** -- potential OOB read on a stream with mid-decode
   resolution changes (see above). Lines 413-479, 544-598.

## Cross-File Observations
This file's comment density (18+ distinct cited prior findings) is the highest of any file audited in this
entire shard -- strong evidence of a subsystem that has already absorbed multiple real review passes; the
one new finding here is a genuinely different class of issue (stale-dimension OOB) from anything the
existing comments already address (which focus on error propagation, resource lifetime, and stateful-retry
correctness, not frame-dimension consistency).

## Missing or Weak Tests
Not independently located in this pass; no test located for a mid-stream resolution change (admittedly a
niche scenario requiring a specially-crafted test fixture).

## Positive Findings
Rigorous, already-hardened error propagation across every FFmpeg call site (send/receive/flush/resample, on
both video and audio paths) -- corrupted or truncated input surfaces as a thrown exception rather than
silently producing garbage or misreporting EOF, a real and non-trivial property for a hand-integrated
decoder wrapper to get right consistently across this many call sites.

## Final Assessment
One MEDIUM-severity finding: `ConvertFrameToRGBA()` and its YUV-conversion helpers trust stale cached
`width_`/`height_` rather than the just-decoded frame's own dimensions, a potential OOB read for a codec/
container that changes resolution mid-stream (low likelihood for this project's actual authored-video-
content use case, but currently unguarded).
