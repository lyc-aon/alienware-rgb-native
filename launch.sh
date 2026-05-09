#!/usr/bin/env bash
# Launcher for Alienware RGB (Native, Qt6).
#
# Hidraw access is granted via /etc/udev/rules.d/99-awelc.rules — the rule
# matches USB 187c:0551 and grants the `plugdev` group rw. No sudo needed.
# If the app ever errors with "cannot open hidraw": replug the controller or
# run `sudo udevadm trigger --subsystem-match=hidraw`.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="$HERE/build/alienware_rgb"

if [[ ! -x "$BIN" ]]; then
  notify-send "Alienware RGB (Native)" "Binary not built at $BIN — run: cd '$HERE' && cmake -S . -B build && cmake --build build -j" 2>/dev/null || true
  echo "missing binary: $BIN" >&2
  exit 1
fi

exec "$BIN" "$@"
