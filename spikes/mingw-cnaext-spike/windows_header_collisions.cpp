// SPDX-License-Identifier: MS-PL
//
// plans/plan_modern.md MOD-1719. A D3D renderer's translation unit includes <windows.h> before it
// includes anything of ours, and <windows.h> is a macro minefield: `near` and `far` are object-like
// macros in windef.h, and `GetObject`, `DrawText` and friends are #defined to their A/W variants.
// Any of those turns a perfectly good declaration in this layer into a syntax error only on Windows
// -- `begin(..., float near, float far)` would be the obvious way to write a graphics header, and it
// would not compile for a single Windows user.
//
// This file is deliberately hostile: it includes <windows.h> with none of the usual defensive
// defines, then every retained graphics-extension header.
//
// Compile-only probe -- there is no main(), and check.sh only ever passes -fsyntax-only.

#ifdef CNA_CNAEXT

#include <windows.h>

#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "CNA/Graphics/CNAEXT.hpp"
#include "CNA/Graphics/CRTEffect.hpp"
#include "CNA/Graphics/CRTMaskType.hpp"
#include "CNA/Graphics/DepthEffect.hpp"
#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/DitherMode.hpp"

// Prove the macros really are live in this translation unit, so the check above is not passing
// because something quietly defined NOMINMAX or WIN32_LEAN_AND_MEAN behind our backs.
#ifndef near
#error "MOD-1719: <windows.h> did not define near -- this probe is not testing what it claims to"
#endif
#ifndef far
#error "MOD-1719: <windows.h> did not define far -- this probe is not testing what it claims to"
#endif
#ifndef GetObject
#error "MOD-1719: <windows.h> did not define GetObject -- this probe is not testing what it claims to"
#endif

#endif // CNA_CNAEXT
