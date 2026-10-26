#!/usr/bin/env bash
# Sends the Tor payload to the console's ELF loader.
#
#   payload/send.sh PS5_IP [PORT]      (PORT defaults to 9021)
#
# Then watch for the "OnionStation5: starting Tor" notification, and later
# "Tor is connected". Logs and settings are in /data/onionstation on the
# console (FTP), see docs/PAYLOAD.md.
set -euo pipefail
host=${1:?usage: payload/send.sh PS5_IP [PORT]}
port=${2:-9021}
elf="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/build/ps5/onionstation-tor.elf"
[ -f "$elf" ] || { echo "send: $elf is missing (run payload/build.sh, or download it from the CI run)" >&2; exit 2; }
python3 - "$host" "$port" "$elf" <<'PY'
import socket, sys
host, port, elf = sys.argv[1], int(sys.argv[2]), sys.argv[3]
with socket.create_connection((host, port), timeout=10) as s, open(elf, "rb") as f:
    s.sendall(f.read())
print(f"send: sent {elf} to {host}:{port}")
PY
