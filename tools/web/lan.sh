#!/bin/bash
# Play the local build from another device of your own on the home network:
# serve.py on 127.0.0.1:8001 inside WSL, reached through a Windows port proxy
# that only the local subnet may use (one-time setup, admin PowerShell):
#
#   netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=8001 connectaddress=127.0.0.1 connectport=8001
#   netsh advfirewall firewall add rule name=xray-wasm-LAN-8001 dir=in action=allow protocol=TCP localport=8001 remoteip=localsubnet profile=any
#
#   lan.sh [docroot]   start (prints the link)
#   lan.sh stop        stop the server
#
# No password by default (home network only); XR_SERVE_PASSWORD=... lan.sh
# requires one. The link serves the game's files: for your own devices only.
# Plain http on a LAN address is not a secure context, so the page runs
# without SharedArrayBuffer (XRFS falls back to sync XHR) unless the device's
# Chrome lists the origin in chrome://flags/#unsafely-treat-insecure-origin-as-secure.

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
STATE="${XDG_RUNTIME_DIR:-/tmp}/xray-wasm-lan"
PORT=8001

stop() {
    if [ -f "$STATE/server.pid" ]; then
        kill "$(cat "$STATE/server.pid")" 2>/dev/null
        rm -f "$STATE/server.pid"
    fi
}

if [ "${1:-}" = "stop" ]; then
    stop
    echo "stopped"
    exit 0
fi

DOCROOT="${1:-$HOME/dev/web}"
mkdir -p "$STATE"
stop

setsid nohup python3 "$HERE/serve.py" "$DOCROOT" "$PORT" > "$STATE/server.log" 2>&1 < /dev/null &
echo $! > "$STATE/server.pid"

POWERSHELL=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
IP="$("$POWERSHELL" -NoProfile -Command '(Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.InterfaceAlias -notmatch "Loopback|vEthernet" -and $_.IPAddress -notlike "169.*" } | Select-Object -First 1).IPAddress' 2>/dev/null | tr -d '\r')"
[ -n "$IP" ] || IP="LAN-address-of-this-PC"
echo "link: http://$IP:$PORT/index.html"
echo "stop: $0 stop"
