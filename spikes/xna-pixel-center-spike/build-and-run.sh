#!/usr/bin/env bash
# Compiles and runs the XNA 4.0 convention probe inside the XNA Wine prefix.
set -u
export WINEPREFIX="$HOME/.wine-cna-xna40"
unset WAYLAND_DISPLAY            # Wine hijacks the window through Wayland otherwise
export DISPLAY="${DISPLAY:-:131}"
export WINEDEBUG=-all

G='C:\windows\Microsoft.NET\assembly'
XG="$G\\GAC_32"
V='v4.0_4.0.0.0__842cf8be1de50553'

wine "$WINEPREFIX/drive_c/windows/Microsoft.NET/Framework/v4.0.30319/csc.exe" \
  /nologo /platform:x86 /target:exe /out:XnaConventionProbe.exe \
  "/r:$XG\\Microsoft.Xna.Framework\\$V\\Microsoft.Xna.Framework.dll" \
  "/r:$XG\\Microsoft.Xna.Framework.Graphics\\$V\\Microsoft.Xna.Framework.Graphics.dll" \
  "/r:$XG\\Microsoft.Xna.Framework.Game\\$V\\Microsoft.Xna.Framework.Game.dll" \
  XnaConventionProbe.cs || { echo "BUILD FAILED"; exit 1; }

echo "--- built, running ---"
wine ./XnaConventionProbe.exe
