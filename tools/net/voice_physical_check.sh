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
set -euo pipefail
build=${1:?build dir}; out=${2:?evidence dir}
harness=$build/cna_net_two_process_harness
mkdir -p "$out"; work=$(mktemp -d "$out/work.XXXX")
host= client= tapper= recorder=
failed=0
cleanup() {
    for child in "$host" "$client" "$tapper" "$recorder"; do
        if [ -n "$child" ]; then
            kill -TERM "$child" 2>/dev/null || true
            wait "$child" 2>/dev/null || true
        fi
    done
    rm -rf "$work"
}
trap cleanup EXIT
status() {
    local label=$1 code=$2
    echo "$label exit=$code" | tee -a "$out/voice-physical.txt"
    if [ "$code" -ne 0 ]; then failed=1; fi
}
wait_child() {
    local label=$1 pid=$2 code=0
    wait "$pid" || code=$?
    status "$label" "$code"
}
stop_recording() {
    local label=$1 pid=$2 code=0
    kill -INT "$pid" 2>/dev/null || true
    wait "$pid" || code=$?
    # pw-record may return 130 for the deliberate SIGINT that finalizes its WAV file.
    if [ "$code" -eq 130 ]; then code=0; fi
    status "$label" "$code"
}
[[ -x "$harness" ]] || { echo "voice-physical: missing executable $harness" >&2; exit 1; }
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
if not len(x):
    raise ValueError("empty recording")
spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
freqs = np.fft.rfftfreq(len(x), 1 / rate)
band = (freqs > float(sys.argv[2]) - 15) & (freqs < float(sys.argv[2]) + 15)
voice = (freqs > 80) & (freqs < 4000)
if not np.isfinite(spectrum).all() or spectrum[voice].sum() <= 0:
    raise ValueError("recording has no finite voice-band energy")
print(f"{spectrum[band].sum() / spectrum[voice].sum():.4f} rms={np.sqrt((x ** 2).mean()):.1f}")
EOF
}
start_host() {
    "$harness" --role=voice-physical-host --timeout=$1 > "$work/host.out" 2> "$work/host.err" &
    host=$!
    for _ in $(seq 100); do grep -q PORT= "$work/host.out" 2>/dev/null && break; sleep 0.1; done
    port=$(sed -n 's/^PORT=//p' "$work/host.out")
    [[ "$port" =~ ^[0-9]+$ ]] && ((port > 0 && port <= 65535)) || {
        echo "voice-physical: host did not publish a valid port" >&2
        return 1
    }
}
wait_joined() {
    for _ in $(seq 100); do grep -q '^JOINED$' "$work/client.out" 2>/dev/null && return 0; sleep 0.1; done
    echo "voice-physical: client did not join" >&2
    return 1
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
wait_joined
# A known tone reaches the microphone only through an unmuted speaker; muted, the microphone hears
# the room, and what is decoded can be checked for arrival but not for content.
if [ "$muted" = no ]; then
    tone=0; pw-play --volume 0.35 "$work/tone1000.wav" || tone=$?
    status "capture tone player" "$tone"
fi
capture=0; wait "$client" || capture=$?; client=
status "capture" "$capture"
wait_child "capture host" "$host"; host=
cat "$work/client.out" | grep -E 'CAPTURE' | tee -a "$out/voice-physical.txt"
echo "capture exit=$capture (a 1 kHz content check applies only with an unmuted speaker)" | tee -a "$out/voice-physical.txt"

echo "== playback: synthetic 433 Hz voice -> SystemLink -> Opus decode -> real output device" | tee -a "$out/voice-physical.txt"
export CNA_VOICE_PHYSICAL=playback
start_host 40
"$harness" --role=voice-physical-client --port="$port" --timeout=8 > "$work/client.out" 2>&1 &
client=$!
wait_joined
sleep 1
tap "$work/sink-during.wav"
[ "$muted" = no ] && { pw-record --target "$source" "$work/room-during.wav" & recorder=$!; }
sleep 4
stop_recording "playback output recorder" "$tapper"; tapper=
if [ -n "$recorder" ]; then stop_recording "playback room recorder" "$recorder"; recorder=; fi
playback=0; wait "$client" || playback=$?; client=
status "playback" "$playback"
wait_child "playback host" "$host"; host=
grep -E 'PLAYBACK' "$work/client.out" | tee -a "$out/voice-physical.txt"
metric=$(share "$work/sink-during.wav" 433)
echo "433 Hz share of what CNA sent to the output device: $metric" | tee -a "$out/voice-physical.txt"
if [ "$muted" = no ]; then
    metric=$(share "$work/room-during.wav" 433)
    echo "433 Hz share heard by the microphone: $metric" | tee -a "$out/voice-physical.txt"
fi
echo "playback exit=$playback" | tee -a "$out/voice-physical.txt"

echo "== Guide system sounds -> real output device" | tee -a "$out/voice-physical.txt"
unset CNA_GAMER_SERVICES_SOUNDS
tap "$work/guide.wav"; sleep 0.3
guide=0; "$harness" --role=guide-sounds | tee -a "$out/voice-physical.txt" || guide=$?
status "guide" "$guide"
sleep 0.3; stop_recording "guide output recorder" "$tapper"; tapper=
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
echo "voice-physical process/analysis failures=$failed (acoustic quality requires review)" | tee -a "$out/voice-physical.txt"
exit "$failed"
