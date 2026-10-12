#!/bin/bash
# Headless-Chrome probe for the wasm port (git-bash on Windows or Linux).
#
# Usage: probe_chrome.sh RUN_SECONDS SHOT_DELAY [EXTRA_URL_PARAMS]
#
# Opens index.html?shot=SHOT_DELAY against the local dev server, collects the
# engine console (stderr) and extracts the ?shot= canvas screenshot into a
# real PNG. index.html also understands a &mouse= parameter to inject
# synthetic SDL mouse events (see index.html).
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

CHROME=""
for candidate in \
    "/c/Program Files/Google/Chrome/Application/chrome.exe" \
    "/c/Program Files (x86)/Google/Chrome/Application/chrome.exe" \
    "/usr/bin/google-chrome" \
    "/usr/bin/chromium"; do
    if [ -x "$candidate" ]; then CHROME="$candidate"; break; fi
done
[ -z "$CHROME" ] && { echo "chrome not found"; exit 1; }

TMP="$(mktemp -d -t xray-probe-XXXXXX)"
LOG="$TMP/chrome.log"
PNG="$TMP/shot.png"
trap 'rm -rf "$TMP" 2>/dev/null || true' EXIT

URL="http://127.0.0.1:8000/index.html?shot=$SHOT_DELAY$EXTRA"
echo "URL: $URL   (run ${RUN_SECS}s, shot at ${SHOT_DELAY}s)"

timeout --kill-after=5 "${RUN_SECS}s" "$CHROME" \
    --headless=new --enable-logging=stderr --v=0 \
    --user-data-dir="$TMP/profile" --no-first-run --window-size=1280,720 \
    --disable-extensions --disable-background-networking --disable-component-update \
    --disable-sync --no-default-browser-check \
    "$URL" 2>"$LOG"

# Console lines without the chrome INFO:CONSOLE prefix.
sed 's/^.*CONSOLE([0-9]*)[^"]*"//; s/", source.*$//' "$LOG" > "$TMP/console.log"
echo "console -> $TMP/console.log ($(wc -l < "$TMP/console.log") lines)"

if grep -q 'CANVAS_SHOT:' "$LOG"; then
    grep -o 'CANVAS_SHOT:data:image/png;base64,[A-Za-z0-9+/=]*' "$LOG" | \
        head -1 | cut -d, -f2- | tr -d '\r\n' | base64 -d > "$PNG" 2>/dev/null
    echo "screenshot -> $PNG ($(stat -c%s "$PNG" 2>/dev/null) bytes)"
else
    echo "no CANVAS_SHOT in log"
fi
