#!/bin/bash
set -e

[ "$EUID" -eq 0 ] || { echo "Run this script with sudo." >&2; exit 1; }
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BOOT=/boot
[ -d /boot/firmware ] && BOOT=/boot/firmware
CONFIG=$BOOT/config.txt
CMDLINE=$BOOT/cmdline.txt

apt update
apt install -y build-essential cmake pkg-config libconfig-dev libusbgx-dev libbluetooth-dev \
    libsystemd-dev bluez bluez-tools xxd rfkill

grep -qxF 'dtoverlay=dwc2' "$CONFIG" || printf '\ndtoverlay=dwc2\n' >> "$CONFIG"
grep -qE '(^| )modules-load=dwc2( |$)' "$CMDLINE" ||
    sed -i '1s/$/ modules-load=dwc2/' "$CMDLINE"
# If Bluetooth pairing is unreliable, uncomment this line:
# printf '\ndtoverlay=disable-wifi\n' >> "$CONFIG"

BUILD_USER=${SUDO_USER:-root}
if [ "$BUILD_USER" = root ]; then
    cmake -S "$ROOT" -B "$ROOT/build"
    cmake --build "$ROOT/build" -j"$(nproc)"
else
    sudo -u "$BUILD_USER" cmake -S "$ROOT" -B "$ROOT/build"
    sudo -u "$BUILD_USER" cmake --build "$ROOT/build" -j"$(nproc)"
fi

install -m755 "$ROOT/build/ds5-bridge" /usr/local/bin/ds5-bridge
install -d -m755 /usr/local/share/ds5dongle
install -m644 "$ROOT/src/dualSense_hid_report.hex" \
    /usr/local/share/ds5dongle/dualSense_hid_report.hex
install -m755 "$ROOT/configfs/ds5-gadget-setup.sh" /usr/local/libexec/ds5-gadget-setup.sh
install -m644 "$ROOT/systemd/ds5-gadget.service" /etc/systemd/system/
install -m644 "$ROOT/systemd/ds5-bridge.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable ds5-gadget ds5-bridge

echo "Install complete. Reboot with PWR IN powered, then plug gadget port into the host."
