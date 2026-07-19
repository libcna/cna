# Audit: src/Microsoft/Xna/Framework/Audio/Microphone.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Audio/Microphone.cpp`
- Audit status: AUDITED (full read, 260 lines)
- Subsystem: `xna-audio` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Audio/Microphone.cs` +
  `src/FNAPlatform/SDL3_FNAPlatform.cs`'s microphone-related methods
- Main related tests: not independently located in this pass

## Purpose
Implements SDL3-audio-capture-device enumeration, lazy device-stream opening (`Start()`), buffered
reads (`GetData()`), and buffer-ready polling (`CheckBuffer()`/`CheckAllBuffers()`).

## Executive Verdict
Correct, and a strong example of proactive overflow hardening. `GetData(buffer, offset, count)`'s
bounds check explicitly cites and fixes the same integer-overflow class already fixed elsewhere in
this codebase (`P9-AUDIT-002`, referencing `P9-VALIDATION-003`), computing `offset+count` only in
`std::size_t` space, never a plain `intcs` addition that could wrap. `GetQueuedBytes()` correctly
diverges from FNA's own SDL2-era `SDL_GetAudioStreamQueued` in favor of SDL3's own recommended
`SDL_GetAudioStreamAvailable` for the "how much can I read right now" query this code actually
needs -- a disclosed, reasoned, verified-correct platform-API update rather than a literal port.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Cites `P9-VALIDATION-003` (originally fixed in `SoundEffect`'s buffer/range constructor and
`DynamicSoundEffectInstance::SubmitBuffer`/`SubmitFloatBufferEXT`) as the source of the overflow-check
pattern applied here -- confirmed consistent with both of those files' own implementations (audited
separately in this shard).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, well-reasoned overflow-safe bounds checking; a disclosed, justified SDL2-to-SDL3 API
substitution (`SDL_GetAudioStreamAvailable` over `SDL_GetAudioStreamQueued`) rather than a blind
port of FNA's own choice.

## Final Assessment
No findings.
