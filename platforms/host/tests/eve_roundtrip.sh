#!/bin/sh
# Boots the firmware against the passive display with trace recording, then replays the trace
# with eve-replay and checks that the boot sequence looks like a real UI start (frames, text,
# fonts).  usage: eve_roundtrip.sh path/to/tracker-host path/to/eve-replay
set -eu
HOST="$1"
REPLAY="$2"
SD="$(mktemp -d)"
TRACE="$SD/boot.evetrace"
trap 'rm -rf "$SD"' EXIT

SDL_AUDIODRIVER=dummy perl -e 'alarm 60; exec @ARGV' "$HOST" --sd "$SD/card" --display=passive \
    --eve-trace "$TRACE" --run-seconds 4 2>/dev/null
test -s "$TRACE" || { echo "no trace recorded"; exit 1; }

OUT="$("$REPLAY" "$TRACE" --stats)"
echo "$OUT"
echo "$OUT" | grep -Eq 'frames \(CMD_SWAP\): [1-9]' || { echo "no frames in trace"; exit 1; }
echo "$OUT" | grep -Eq 'CMD_TEXT +[1-9]' || { echo "no text commands in trace"; exit 1; }
echo "$OUT" | grep -Eq 'CMD_SETFONT2 +[1-9]' || { echo "no font setup in trace"; exit 1; }
echo "$OUT" | grep -q 'CMD_UNKNOWN' && { echo "command stream has undecodable commands"; exit 1; }
echo "eve roundtrip OK"
