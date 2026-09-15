# dr_libs (dr_mp3, dr_flac)

Single-file audio decoders by David Reid, <https://github.com/mackron/dr_libs>, each under the
choice of public domain (Unlicense) or MIT No Attribution -- the full statements are at the end of
each file.

| File | Version | SHA-256 |
|---|---|---|
| `dr_mp3.h` | v0.7.3, 2026-01-17 | `d80e6800ddde29d50988ce894affe601858802d1bb8f047f98b84b33ae66c11a` |
| `dr_flac.h` | v0.13.3, 2026-01-17 | `a35468f278f17cf9538ce189debf76a5854a77e139980f7b5185d67566a0c837` |

Byte-identical to the copies SDL_mixer ships (`third_party/SDL_mixer/src/dr_libs/`), taken from
there rather than from the submodule at build time so that CNA's own mixer -- the SDL-free audio
path, `CNA_AUDIO_PLATFORM=ALSA` -- builds from a checkout without submodules
(`plans/plan_x11.md` X11-0161). They are compiled in one translation unit,
`modules/audio/src/Backend/CnaMixer/DrLibs.cpp`, with warnings off, and never edited here.
