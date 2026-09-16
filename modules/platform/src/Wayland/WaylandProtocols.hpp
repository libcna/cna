// SPDX-License-Identifier: MS-PL
#pragma once

// The one place the Wayland backend includes libwayland and the generated protocol bindings
// (plans/plan_wayland.md D-2). Every optional protocol is behind the CNA_WAYLAND_HAVE_* definition
// cmake/PlatformWayland.cmake publishes when its XML was found, so a build against an older
// wayland-protocols compiles with that capability absent rather than failing.
//
// None of this reaches a public header: the contract carries a Wayland window as an opaque
// `void* display` plus `void* surface`, and that is the backend's entire public surface.

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
#include "xdg-output-unstable-v1-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
#include "viewporter-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
#include "fractional-scale-v1-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
#include "relative-pointer-unstable-v1-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
#include "pointer-constraints-unstable-v1-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
#include "text-input-unstable-v3-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
#include "primary-selection-unstable-v1-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
#include "xdg-decoration-unstable-v1-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_ACTIVATION)
#include "xdg-activation-v1-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
#include "idle-inhibit-unstable-v1-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
#include "xdg-foreign-unstable-v2-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_TABLET)
#include "tablet-v2-client-protocol.h"
#endif
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
// cursor-shape-v1 names zwp_tablet_tool_v2, so its header needs tablet-v2's; the two are found
// and generated together in cmake/PlatformWayland.cmake.
#include "cursor-shape-v1-client-protocol.h"
#endif
