#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_xnapipeline_parity.md XNAPP-280: run the interoperability harness against a genuine
# Microsoft XNA 4.0 runtime.
#
# The harness is a Windows program and XNA 4.0 is Windows-only, so this runs it under Wine against
# the XNA 4.0 Refresh runtime installed in a prefix. It needs an X display because the harness
# makes a real GraphicsDevice; a private one is fine. As every other GPU/window test does
# (cmake/TestDisplayPolicy.cmake), it uses the display it was launched with -- under
# tools/platform/run_gpu_tests_private.sh, the private rootful Xwayland. It used to pin :99 over
# the caller's DISPLAY, and inside the private runner nothing listens there: Wine's explorer.exe
# could not open it, no display driver loaded, and `new Game()` died with "Error creating window
# handle" -- in the runner, and on every machine without an Xvfb parked on :99.
#
# It also needs an audio endpoint, not a speaker: loading a SoundEffect initialises XAudio2, and
# with no endpoint at all XNA's InitializeSingletonXAudio2 dies with an AccessViolationException.
# Inside the private runner there was none -- PulseAudio's socket lives in the session's
# XDG_RUNTIME_DIR, which the runner replaces, and ALSA's default is a PipeWire plugin that is not
# installed for 32-bit. So Wine gets ALSA's built-in null device as its only endpoint, the way
# CNA's own tests get SDL_AUDIODRIVER=dummy, and never reaches the owner's audio session.
#
#   CNA_XNA40_REFERENCES  directory holding Microsoft.Xna.Framework*.dll
#   CNA_XNA40_WINEPREFIX  Wine prefix with .NET Framework 4.0 and the XNA runtime
#   CNA_XNA40_DISPLAY     X display (default: the caller's DISPLAY, else :99)
#   $1                    fixture directory (default tests/assets/xnb/cna/windows/uncompressed)
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
fixtures="${1:-$repo/tests/assets/xnb/cna/windows/uncompressed}"
refs="${CNA_XNA40_REFERENCES:-/rv/tmp/samples/_tools/xna-game-studio-4-refresh/admin/Program Files/Microsoft XNA/XNA Game Studio/v4.0/References/Windows/x86}"
prefix="${CNA_XNA40_WINEPREFIX:-$HOME/.wine-cna-xna40}"
build="$repo/build/xna-interop"

command -v mcs >/dev/null  || { echo "run-interop-harness: mcs not found" >&2; exit 77; }
command -v wine >/dev/null || { echo "run-interop-harness: wine not found" >&2; exit 77; }
for dll in Microsoft.Xna.Framework.dll Microsoft.Xna.Framework.Graphics.dll Microsoft.Xna.Framework.Game.dll Microsoft.Xna.Framework.Video.dll; do
    [ -f "$refs/$dll" ] || { echo "run-interop-harness: missing $refs/$dll" >&2; exit 77; }
done
[ -d "$fixtures" ] || { echo "run-interop-harness: no fixtures at $fixtures" >&2; exit 77; }

# The Microsoft assemblies are copied only into the ignored build directory, beside the harness,
# so the CLR finds them without a GAC; nothing Microsoft owns reaches the repository.
rm -rf "$build"; mkdir -p "$build"
cp "$refs/Microsoft.Xna.Framework.Video.dll" "$build/" 2>/dev/null || true
cp "$refs/Microsoft.Xna.Framework.dll" "$refs/Microsoft.Xna.Framework.Graphics.dll" \
   "$refs/Microsoft.Xna.Framework.Game.dll" "$build/"
cp "$fixtures"/* "$build/"

mcs -sdk:4 -platform:x86 -target:exe -nologo -out:"$build/CnaXnbInterop.exe" \
    -r:"$build/Microsoft.Xna.Framework.dll" -r:"$build/Microsoft.Xna.Framework.Graphics.dll" \
    -r:"$build/Microsoft.Xna.Framework.Game.dll" \
    -r:"$build/Microsoft.Xna.Framework.Video.dll" "$here/Program.cs"

# Wine is held to X11. WAYLAND_DISPLAY is set EMPTY, not unset: with it unset, Wine's default
# driver list (x11, then wayland) falls back -- whenever the X display cannot be opened -- to
# libwayland's default socket, $XDG_RUNTIME_DIR/wayland-0, which outside the private runner is the
# owner's live desktop. Empty is refused; it is the same guard ctest gives every test
# (CNA_TEST_WAYLAND_GUARD). Both Wine invocations get the same environment, so the wineserver and
# its explorer.exe, started by whichever comes first, are on the display the harness then uses.
#
# Audio: PULSE_SERVER names a socket that does not exist, so winepulse is unavailable and Wine
# falls back to winealsa; ALSA_CONFIG_PATH names ONLY the file below, because alsa.conf's own hooks
# load conf.d (the PipeWire default) after every listed file and would override it. winealsa then
# finds `default`, the null device, and reports `Invalid CTL hw:N` for the sound cards this config
# deliberately does not describe -- noise, not a failure.
#
# winedbg.exe is disabled. On an unhandled exception Wine starts `winedbg --auto`, and once a
# display driver loads that opens a modal "Program Error" dialog and waits for a click nobody will
# give: the test then hangs for its whole ctest TIMEOUT instead of failing with the CLR's stack
# trace, which is already printed before the debugger would start.
printf 'pcm.!default { type null }\n' > "$build/alsa-null.conf"
display="${CNA_XNA40_DISPLAY:-${DISPLAY:-:99}}"
wine_env=(WAYLAND_DISPLAY= DISPLAY="$display"
          PULSE_SERVER=unix:/nonexistent/cna-xna-interop-no-pulse-server
          ALSA_CONFIG_PATH="$build/alsa-null.conf"
          WINEDLLOVERRIDES=winedbg.exe=d
          WINEPREFIX="$prefix" WINEDEBUG=-all)
win_fix="$(env "${wine_env[@]}" wine winepath -w "$build" 2>/dev/null)"
env "${wine_env[@]}" wine "$build/CnaXnbInterop.exe" "$win_fix"
