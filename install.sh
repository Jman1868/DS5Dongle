#!/usr/bin/env bash
set -euo pipefail

[[ $EUID -eq 0 ]] || { echo "Run this script with sudo." >&2; exit 1; }
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
CONFIG=/boot/firmware/config.txt
CMDLINE=/boot/firmware/cmdline.txt

apt update
apt install -y build-essential cmake pkg-config libusbgx-dev libbluetooth-dev bluez bluez-tools

grep -qxF 'dtoverlay=dwc2' "$CONFIG" || printf '\ndtoverlay=dwc2\n' >> "$CONFIG"
grep -qE '(^| )modules-load=dwc2( |$)' "$CMDLINE" ||
    sed -i '1s/$/ modules-load=dwc2/' "$CMDLINE"
# If Bluetooth pairing is unreliable, uncomment the following line:
# printf '\ndtoverlay=disable-wifi\n' >> "$CONFIG"

cmake -S "$ROOT" -B "$ROOT/build"
cmake --build "$ROOT/build" --parallel
install -Dm755 "$ROOT/build/ds5-bridge" /usr/local/bin/ds5-bridge
install -Dm755 "$ROOT/configfs/ds5-gadget-setup.sh" /usr/local/libexec/ds5-gadget-setup.sh
install -Dm644 "$ROOT/src/dualSense_hid_report.hex" /usr/local/share/ds5dongle/dualSense_hid_report.hex
install -Dm644 "$ROOT/systemd/ds5-gadget.service" /etc/systemd/system/ds5-gadget.service
install -Dm644 "$ROOT/systemd/ds5-bridge.service" /etc/systemd/system/ds5-bridge.service
systemctl daemon-reload
systemctl enable ds5-gadget.service ds5-bridge.service

cat <<'EOF'
Installation complete. Reboot the Pi with its power connected to the PWR IN
port, then connect the PC to the Pi's USB gadget port.
EOF
