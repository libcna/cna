#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# Runs a command against a locally built, ES1-capable Mesa.
#
# Debian (and most desktop distros) build Mesa with `-Dgles1=disabled`, so the
# stock system Mesa cannot create an OpenGL ES 1.1 context on ANY driver --
# radeonsi on real hardware fails exactly like llvmpipe and softpipe do. See
# docs/opengles1-backend.md for the full root-cause writeup and for the
# (rootless) instructions to produce the Mesa build this script expects.
#
# Usage:
#   scripts/opengles1-test-env.sh ctest --test-dir cmake-build-opengles1 -R OpenGLES1
#   scripts/opengles1-test-env.sh ./cmake-build-opengles1/cna_test_opengles1_clear_readback
#
# Override the Mesa location with CNA_ES1_MESA_PREFIX if it is not the default.

set -euo pipefail

MESA_PREFIX="${CNA_ES1_MESA_PREFIX:-$HOME/deps/mesa-es1-install}"
MESA_LIBDIR="$MESA_PREFIX/lib/x86_64-linux-gnu"

if [ ! -d "$MESA_LIBDIR" ]; then
    echo "error: no ES1-capable Mesa at $MESA_PREFIX" >&2
    echo "       build one first -- see docs/opengles1-backend.md" >&2
    echo "       or set CNA_ES1_MESA_PREFIX to an existing build." >&2
    exit 1
fi

export LD_LIBRARY_PATH="$MESA_LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export __EGL_VENDOR_LIBRARY_DIRS="$MESA_PREFIX/share/glvnd/egl_vendor.d"
export LIBGL_DRIVERS_PATH="$MESA_LIBDIR/dri"
export __GLX_VENDOR_LIBRARY_NAME=mesa

# This Mesa is built with the software gallium drivers only, which is all the
# backend needs -- ES1 conformance here is about the fixed-function pipeline,
# not about GPU throughput.
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER="${GALLIUM_DRIVER:-softpipe}"

# SDL3 picks Wayland over X11 when WAYLAND_DISPLAY is set, which bypasses the
# GLX path this backend goes through; force the X11 driver deliberately.
unset WAYLAND_DISPLAY
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"

# Default to the virtual display so test runs do not pop windows onto the developer's real
# desktop. Export DISPLAY yourself (e.g. DISPLAY=:0) if you deliberately want to watch them.
export DISPLAY="${DISPLAY:-:99}"

exec "$@"
