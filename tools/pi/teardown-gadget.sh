#!/bin/sh
set -eu
G=/sys/kernel/config/usb_gadget/ds5dongle
if [ "$(id -u)" -ne 0 ]; then echo "ERROR: run as root (sudo $0)" >&2; exit 1; fi
if [ ! -d "$G" ]; then echo "UNCHANGED: gadget is not configured"; exit 0; fi
if [ -e "$G/UDC" ]; then printf '' > "$G/UDC"; fi
rm -f "$G/configs/c.1/hid.usb0"
rmdir "$G/functions/hid.usb0" 2>/dev/null || true
rmdir "$G/configs/c.1/strings/0x409" "$G/configs/c.1" 2>/dev/null || true
rmdir "$G/strings/0x409" "$G" 2>/dev/null || true
echo "REMOVED: /sys/kernel/config/usb_gadget/ds5dongle"
