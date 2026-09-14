// SPDX-License-Identifier: MS-PL
#pragma once

// The one place `<windows.h>` enters this module, and the one place its two hostile default
// behaviours are turned off.
//
// NOMINMAX: without it `<windows.h>` defines `min`/`max` as macros, which breaks `<algorithm>`,
// `<limits>` and `std::numeric_limits<...>::max()` in every translation unit that includes both.
// WIN32_LEAN_AND_MEAN: drops the RPC/OLE/socket surface nothing here uses, which is compile time
// rather than correctness -- but it also stops `<winsock.h>` from pre-empting `<winsock2.h>`.
//
// Every Win32 source in this directory includes this header instead of `<windows.h>` directly,
// so the guards cannot be forgotten in one file and silently fixed by include order in another.

// The wide Win32 API is the one this backend uses: every call is the ...W form, and every string
// that crosses the boundary is converted through Win32Utf. UNICODE additionally makes the
// resource macros (IDC_ARROW and friends) expand to their wide form, which is what lets
// LoadCursorW take them without a cast.
#if !defined(UNICODE)
#  define UNICODE
#endif
#if !defined(_UNICODE)
#  define _UNICODE
#endif

#if !defined(NOMINMAX)
#  define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#  define WIN32_LEAN_AND_MEAN
#endif

// Target Windows 10 1703 for the per-monitor-v2 DPI declarations. This raises what the headers
// *declare*, never what the binary *requires*: every entry point above the Windows 7 floor is
// resolved at run time through Win32DpiSupport, so the produced binary still loads on an older
// host and degrades to the documented fallback.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0603
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0603
#endif
#if !defined(WINVER) || WINVER < 0x0603
#  undef WINVER
#  define WINVER 0x0603
#endif

#include <windows.h>

// --- Macro collisions with the CNA platform contract -------------------------------------------
//
// `<windows.h>` defines a family of `#define Name NameW` aliases, and four of them collide with
// identifiers the CNA platform contract already uses:
//
//   CreateWindow     -> IPlatform::CreateWindow
//   CreateDirectory  -> IPlatformFileSystem::CreateDirectory
//   MessageBox       -> PlatformCapability::MessageBox
//   GetClassName     -> Win32WindowClass::GetClassName
//   FindWindow       -> Win32PlatformAccess::FindWindow
//
// A preprocessor macro does not respect namespaces or member scope, so with them in force a
// declaration and its definition are silently renamed to `CreateDirectoryW` and the `override`
// no longer matches anything -- which is exactly the diagnostic this replaced.
//
// Undefining them here is safe and complete for this module: every Win32 source includes this
// header, every Win32 call in the module is spelled with its explicit `...W` suffix, and nothing
// outside relies on the generic spelling. It also means the contract keeps its own names rather
// than being reshaped by a platform header, which is the direction the dependency is supposed to
// run. A Win32 *host application* that includes both `<windows.h>` and CNA's platform headers
// needs the same four lines -- see docs/platform-win32.md.
#undef CreateWindow
#undef CreateDirectory
#undef MessageBox
#undef GetClassName
#undef FindWindow
