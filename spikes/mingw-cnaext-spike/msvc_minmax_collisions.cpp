// SPDX-License-Identifier: MS-PL
//
// plans/plan_modern.md MOD-1719, the half MinGW alone cannot test.
//
// MinGW-w64 guards windef.h's `min`/`max` macros with `#ifndef __cplusplus`, so a C++ translation
// unit never sees them. MSVC's windef.h does not, and MSVC is the compiler a real D3D build uses.
// This file reinstates them by hand so the hazard is actually exercised.
//
// The standard library has to be parsed before they exist: MSVC's STL is hardened against these two
// macros, libstdc++ is not, and pulling <algorithm> in afterwards produces hundreds of errors inside
// <tr1/ell_integral.tcc> that say nothing about CNA. <bits/stdc++.h> is a GCC extension and would be
// wrong in shipping code; here it is exactly right, because the point is to take the standard
// library out of the blast radius and leave only our own headers in it.
//
// check.sh does not require this file to compile. It requires that whatever fails does not live in
// modules/graphics-ext -- see the note there and in plans/plan_modern.md MOD-1719.

#ifdef CNA_CNAEXT

#include <windows.h>
#include <bits/stdc++.h>

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) < (b)) ? (a) : (b))

#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "CNA/Graphics/CNAEXT.hpp"
#include "CNA/Graphics/CRTEffect.hpp"
#include "CNA/Graphics/CRTMaskType.hpp"
#include "CNA/Graphics/DepthEffect.hpp"
#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/DitherMode.hpp"

#endif // CNA_CNAEXT
