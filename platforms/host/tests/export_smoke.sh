#!/bin/sh
# Boots the host build headless, imports a generated sine sample, adds notes to pattern 1,
# exports the song through the firmware exporter and checks that the WAV is a 220 Hz tone.
# usage: export_smoke.sh [path/to/tracker-host]
set -eu
BIN="${1:-$(dirname "$0")/../../../build-host/tracker-host}"
SD="$(mktemp -d)"
trap 'rm -rf "$SD"' EXIT

perl -e 'alarm 180; exec @ARGV' "$BIN" --sd "$SD" --demo-song --export song

python3 -I - "$SD" <<'PY'
import glob, struct, sys, wave
files = glob.glob(sys.argv[1] + "/Export/*/*/master.wav")
assert len(files) == 1, "expected one master.wav, found %r" % files
w = wave.open(files[0])
assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (2, 2, 44100), "bad WAV format"
n = w.getnframes()
data = struct.unpack("<%dh" % (n * 2), w.readframes(n))
left = data[0::2]
peak = max(abs(x) for x in data)
assert 3.5 < n / 44100 < 4.5, "unexpected length %.2fs" % (n / 44100)
assert peak > 2000, "export is (nearly) silent, peak=%d" % peak
seg = left[:11025]
hz = sum(1 for a, b in zip(seg, seg[1:]) if a < 0 <= b) * 4
assert 190 < hz < 250, "wrong pitch, ~%d Hz" % hz
print("export smoke test OK: %.2fs, peak %d, ~%d Hz" % (n / 44100, peak, hz))
PY
