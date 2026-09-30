#!/usr/bin/env bash
# SPDX-License-Identifier: MS-PL
# GSH-16: NetworkSession voice through this machine's real microphone and speaker (PipeWire).
#
#   tools/net/voice_physical_check.sh <build-dir> <evidence-dir>
#
# capture:  a 1 kHz tone plays from the speaker; a host process hears it through the real
#           microphone (speech detection, Opus, SystemLink), and a client process checks that what
#           it decodes is that tone.
# playback: a host process talks with a synthetic 433 Hz voice; a client process plays it through
#           the real speaker, and the real microphone records the room while it does.
# The tone is quiet and lasts a few seconds. Recordings are analysed and deleted; only numbers stay.
set -uo pipefail
build=${1:?build dir}; out=${2:?evidence dir}
harness=$build/cna_net_two_process_harness
mkdir -p "$out"; work=$(mktemp -d "$out/work.XXXX"); trap 'rm -rf "$work"' EXIT
export CNA_GAMER_SERVICES_SOUNDS=0
python3 - "$work/tone1000.wav" <<'EOF'
import math, struct, sys, wave
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
    w.writeframes(b"".join(struct.pack("<h", int(12000 * math.sin(2 * math.pi * 1000 * i / 48000))) for i in range(48000 * 5)))
EOF
share() { python3 - "$1" "$2" <<'EOF'
import sys, wave
import numpy as np
with wave.open(sys.argv[1]) as w:
    rate = w.getframerate(); channels = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
x = x.reshape(-1, channels).mean(axis=1) if channels > 1 else x
spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
freqs = np.fft.rfftfreq(len(x), 1 / rate)
band = (freqs > float(sys.argv[2]) - 15) & (freqs < float(sys.argv[2]) + 15)
voice = (freqs > 80) & (freqs < 4000)
print(f"{spectrum[band].sum() / spectrum[voice].sum():.4f} rms={np.sqrt((x ** 2).mean()):.1f}")
EOF
}
start_host() {
    "$harness" --role=voice-physical-host --timeout=$1 > "$work/host.out" 2> "$work/host.err" &
    host=$!
    for _ in $(seq 100); do grep -q PORT= "$work/host.out" 2>/dev/null && break; sleep 0.1; done
    port=$(sed -n 's/^PORT=//p' "$work/host.out")
}

sink=$(pactl get-default-sink); source=$(pactl get-default-source)
muted=$(pactl get-sink-mute "$sink" | awk '{print $2}')
tap() { pw-record -P '{ stream.capture.sink = true }' --target "$sink" "$1" & tapper=$!; }
{ echo "sink $sink (muted: $muted), source $source"; } | tee "$out/voice-physical.txt"

echo "== capture: real microphone -> speech detection -> Opus -> SystemLink -> decoded" | tee -a "$out/voice-physical.txt"
export CNA_VOICE_PHYSICAL=capture
start_host 40
"$harness" --role=voice-physical-client --port="$port" --timeout=25 > "$work/client.out" 2>&1 &
client=$!
for _ in $(seq 100); do grep -q JOINED "$work/client.out" 2>/dev/null && break; sleep 0.1; done
# A known tone reaches the microphone only through an unmuted speaker; muted, the microphone hears
# the room, and what is decoded can be checked for arrival but not for content.
[ "$muted" = no ] && pw-play --volume 0.35 "$work/tone1000.wav"
wait $client; capture=$?; wait $host
cat "$work/client.out" | grep -E 'CAPTURE' | tee -a "$out/voice-physical.txt"
echo "capture exit=$capture (a 1 kHz content check applies only with an unmuted speaker)" | tee -a "$out/voice-physical.txt"

echo "== playback: synthetic 433 Hz voice -> SystemLink -> Opus decode -> real output device" | tee -a "$out/voice-physical.txt"
export CNA_VOICE_PHYSICAL=playback
start_host 40
"$harness" --role=voice-physical-client --port="$port" --timeout=8 > "$work/client.out" 2>&1 &
client=$!
for _ in $(seq 100); do grep -q JOINED "$work/client.out" 2>/dev/null && break; sleep 0.1; done
sleep 1
tap "$work/sink-during.wav"
[ "$muted" = no ] && { pw-record --target "$source" "$work/room-during.wav" & recorder=$!; }
sleep 4; kill -INT $tapper ${recorder:-} 2>/dev/null; wait $tapper ${recorder:-} 2>/dev/null
wait $client; playback=$?; wait $host
grep -E 'PLAYBACK' "$work/client.out" | tee -a "$out/voice-physical.txt"
echo "433 Hz share of what CNA sent to the output device: $(share "$work/sink-during.wav" 433)" | tee -a "$out/voice-physical.txt"
[ "$muted" = no ] && echo "433 Hz share heard by the microphone: $(share "$work/room-during.wav" 433)" | tee -a "$out/voice-physical.txt"
echo "playback exit=$playback" | tee -a "$out/voice-physical.txt"

echo "== Guide system sounds -> real output device" | tee -a "$out/voice-physical.txt"
unset CNA_GAMER_SERVICES_SOUNDS
tap "$work/guide.wav"; sleep 0.3
"$harness" --role=guide-sounds | tee -a "$out/voice-physical.txt"
sleep 0.3; kill -INT $tapper; wait $tapper 2>/dev/null
python3 - "$work/guide.wav" <<'EOF2' | tee -a "$out/voice-physical.txt"
import sys, wave
import numpy as np
with wave.open(sys.argv[1]) as w:
    rate = w.getframerate(); x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float).reshape(-1, w.getnchannels()).mean(axis=1)
block = rate // 20
loud = [np.sqrt((x[i:i + block] ** 2).mean()) for i in range(0, len(x) - block, block)]
bursts = sum(1 for a, b in zip([0.0] + loud, loud) if a < 150 <= b)
print(f"guide sounds at the output device: {bursts} bursts, peak rms {max(loud):.0f}, quiet rms {np.percentile(loud, 10):.0f}")
EOF2
