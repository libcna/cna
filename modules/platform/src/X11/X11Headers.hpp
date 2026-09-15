// SPDX-License-Identifier: MS-PL
#pragma once

// =================================================================================================
// The single place this backend includes X headers.
//
// ### Why every X11 source goes through this file
//
// `<X11/X.h>` defines `None` as the object-like macro `0L`. CNA's platform contract has five
// enumerators called `None` — `KeyCode::None`, `KeyModifier::None`, `ScreenOrientation::None`,
// `WindowRenderIntent::None` and `PresentScaleMode::None` — and after the macro is in scope
// `KeyCode::None` preprocesses to `KeyCode::0L`, which is a syntax error a hundred lines from
// anything that mentions X11. Every backend that mixes Xlib with a modern C++ API hits this.
//
// So the macro is captured into a typed constant and then undefined. Inside this backend, X11's
// null resource is `kNone`; CNA's enumerators keep their own names and keep working.
//
// `None` is not the only one. `<X11/Xlib.h>` also defines `Bool` and `Status` as object-like
// macros (`#define Bool int`), which is a 1985 C header doing what C had instead of typedefs. A
// C++ header included afterwards that declares anything named `Bool` stops compiling: GoogleTest's
// `testing::Bool()` parameter generator is the concrete case, and it fails with
// "expected unqualified-id before ')'" a hundred lines from anything mentioning X11. So those two
// are captured as real type aliases and undefined as well, which is what lets a test include this
// backend's headers and `<gtest/gtest.h>` in the same translation unit.
//
// Every X header the backend can use is included here, before the `#undef`s, because a header
// included later would re-introduce the macros -- and because several of them *use* the macros in
// their own declarations: `<X11/extensions/Xrandr.h>` uses `None`, and `<GL/glx.h>` declares
// functions returning `Bool`.
// =================================================================================================

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>

#if defined(CNA_X11_HAVE_XI)
#  include <X11/extensions/XInput2.h>
#endif
#if defined(CNA_X11_HAVE_XRANDR)
#  include <X11/extensions/Xrandr.h>
#endif
#if defined(CNA_X11_HAVE_XCURSOR)
#  include <X11/Xcursor/Xcursor.h>
#endif
#if defined(CNA_X11_HAVE_XFIXES)
#  include <X11/extensions/Xfixes.h>
#endif
#if defined(CNA_X11_HAVE_XAU)
#  include <X11/Xauth.h>
#endif
#if defined(CNA_X11_HAVE_XSHM)
#  include <X11/extensions/XShm.h>
#  include <sys/ipc.h>
#  include <sys/shm.h>
#endif
#if defined(CNA_X11_HAVE_GLX)
// Included here rather than at its use site because glXMakeCurrent and friends are declared
// returning `Bool`, which stops existing four lines below this block.
#  include <GL/glx.h>
#endif

namespace CNA::Platform::X11 {

    /**
     * @brief X11's `None` — the null resource id — as a typed constant.
     *
     * Captured from the macro immediately before it is undefined, so the value is X11's own and
     * not a restatement of it. Every X resource type (`Window`, `Atom`, `Colormap`, `Cursor`,
     * `Pixmap`, `Drawable`) is an `XID`, so one constant covers all of them.
     */
    inline constexpr XID kNone = None;

    /** @brief X11's `CurrentTime`, as a typed constant for the same reason as @ref kNone. */
    inline constexpr Time kCurrentTime = CurrentTime;

    /**
     * @brief X11's `Bool`, as a real type rather than a macro spelling of `int`.
     *
     * Xlib's own `Bool` is `#define Bool int`. Keeping the macro would break every C++ header
     * included after this one that happens to declare something called `Bool`.
     */
    using XBool = int;

    /** @brief X11's `Status`, as a real type, for the same reason as @ref XBool. */
    using XStatus = int;

    /** @brief X11's `True`. */
    inline constexpr XBool kXTrue = True;

    /** @brief X11's `False`. */
    inline constexpr XBool kXFalse = False;

} // namespace CNA::Platform::X11

#undef None
#undef CurrentTime
#undef Bool
#undef Status
