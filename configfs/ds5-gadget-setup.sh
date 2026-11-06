#!/usr/bin/env bash
set -euo pipefail

G=/sys/kernel/config/usb_gadget/ds5dongle
HEX_FILE=/usr/local/share/ds5dongle/dualSense_hid_report.hex

modprobe libcomposite
mountpoint -q /sys/kernel/config || mount -t configfs none /sys/kernel/config
mkdir -p "$G"
printf '0x054c' > "$G/idVendor"
printf '0x0ce6' > "$G/idProduct"
printf '0x0200' > "$G/bcdUSB"
printf '0x0100' > "$G/bcdDevice"
mkdir -p "$G/strings/0x409"
printf '054C-DualSense-Dongle' > "$G/strings/0x409/serialnumber"
printf 'Sony Interactive Entertainment' > "$G/strings/0x409/manufacturer"
printf 'DualSense Wireless Controller' > "$G/strings/0x409/product"
mkdir -p "$G/configs/c.1/strings/0x409"
printf 'DualSense' > "$G/configs/c.1/strings/0x409/configuration"
printf 250 > "$G/configs/c.1/MaxPower"
mkdir -p "$G/functions/hid.usb0"
printf 1 > "$G/functions/hid.usb0/protocol"
printf 0 > "$G/functions/hid.usb0/subclass"
printf 64 > "$G/functions/hid.usb0/report_length"
if [[ ! -s "$G/functions/hid.usb0/report_desc" ]]; then
    : > "$G/functions/hid.usb0/report_desc"
    while read -r byte; do printf '%b' "\\x$byte"; done < <(tr ' ' '\n' < "$HEX_FILE") > "$G/functions/hid.usb0/report_desc"
fi
ln -sfn "$G/functions/hid.usb0" "$G/configs/c.1/hid.usb0"
if [[ -z "$(cat "$G/UDC" 2>/dev/null || true)" ]]; then
    UDC=$(find /sys/class/udc -maxdepth 1 -mindepth 1 -printf '%f\n' | head -n1)
    [[ -n "$UDC" ]] || { echo "No USB device controller found (is dwc2 loaded?)" >&2; exit 1; }
    printf '%s' "$UDC" > "$G/UDC"
fi
