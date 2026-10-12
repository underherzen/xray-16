#!/bin/bash
# Headless-Chrome probe for the wasm port (git-bash on Windows or Linux).
#
# Usage: probe_chrome.sh RUN_SECONDS SHOT_DELAY[,SHOT_DELAY...] [EXTRA_URL_PARAMS] [OUT_DIR]
#
# Opens index.html?shot=... against the local dev server, collects the
# engine console (stderr) and extracts every ?shot= canvas screenshot into
# real PNGs. index.html also understands &mouse=/&keys= (synthetic input),
# &newgame=1 (autostart a new game), &mem=N (heap size logging).
#
# Artifacts (console.log, shot_at_Ns.png) are written to OUT_DIR
# (default: probe_out/ under the current directory).
#
# CHROME_EXTRA (env) is appended to the chrome flags, e.g. the real GPU
# instead of SwiftShader: CHROME_EXTRA="--use-angle=d3d11 --ignore-gpu-blocklist"
#
# Notes:
#  - real-time run (no --virtual-time-budget): the engine's main loop is
#    driven by rAF, which virtual time does not pump after exhaustion
#  - each run uses a fresh --user-data-dir; leftovers are best-effort removed
#    (crashpad keeps some files busy -- harmless)

set -u
RUN_SECS="${1:-240}"
SHOT_DELAY="${2:-150}"
EXTRA="${3:-}"
OUT_DIR="${4:-probe_out}"

CHROME=""
for candidate in \
    "/c/Program Files/Google/Chrome/Application/chrome.exe" \
    "/c/Program Files (x86)/Google/Chrome/Application/chrome.exe" \
    "/usr/bin/google-chrome" \
    "/usr/bin/chromium"; do
    if [ -x "$candidate" ]; then CHROME="$candidate"; break; fi
done
[ -z "$CHROME" ] && { echo "chrome not found"; exit 1; }

mkdir -p "$OUT_DIR"
TMP="$(mktemp -d -t xray-probe-XXXXXX)"
LOG="$TMP/chrome.log"
trap 'rm -rf "$TMP" 2>/dev/null || true' EXIT

URL="http://127.0.0.1:8000/index.html?shot=$SHOT_DELAY$EXTRA"
echo "URL: $URL   (run ${RUN_SECS}s, shots at ${SHOT_DELAY}s)"
echo "artifacts -> $OUT_DIR"

timeout --kill-after=10 "${RUN_SECS}s" "$CHROME" \
    --headless=new --enable-logging=stderr --v=0 \
    --user-data-dir="$TMP/profile" --no-first-run --window-size=1280,720 \
    --disable-extensions --disable-background-networking --disable-component-update \
    --disable-sync --no-default-browser-check \
    ${CHROME_EXTRA:-} \
    "$URL" 2>"$LOG"
RC=$?
echo "chrome exit: $RC"

# Console lines without the chrome INFO:CONSOLE prefix.
sed 's/^.*CONSOLE([0-9]*)[^"]*"//; s/", source.*$//' "$LOG" > "$OUT_DIR/console.log"
echo "console -> $OUT_DIR/console.log ($(wc -l < "$OUT_DIR/console.log") lines)"

# Every CANVAS_SHOT[at_Ns]:data:image/png;base64,.... becomes shot_at_Ns.png
grep -o 'CANVAS_SHOT\[at_[0-9]*s\]:data:image/png;base64,[A-Za-z0-9+/=]*' "$LOG" | \
while IFS= read -r line; do
    tag="${line%%]*}"           # CANVAS_SHOT[at_150s
    tag="${tag##*[}"            # at_150s
    printf '%s' "$line" | cut -d, -f2- | tr -d '\r\n' | base64 -d > "$OUT_DIR/shot_${tag}.png" 2>/dev/null
done
if ls "$OUT_DIR"/shot_at_*.png >/dev/null 2>&1; then
    for f in "$OUT_DIR"/shot_at_*.png; do
        echo "screenshot -> $f ($(stat -c%s "$f" 2>/dev/null) bytes)"
    done
else
    echo "no CANVAS_SHOT in log"
fi
