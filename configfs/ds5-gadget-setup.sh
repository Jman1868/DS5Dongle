#!/bin/bash
set -e

G=/sys/kernel/config/usb_gadget/ds5dongle
HEX=/usr/local/share/ds5dongle/dualSense_hid_report.hex

command -v xxd >/dev/null || { echo "ds5-gadget-setup: xxd is required" >&2; exit 1; }
[ -f "$HEX" ] || { echo "ds5-gadget-setup: missing $HEX" >&2; exit 1; }

modprobe libcomposite
mountpoint -q /sys/kernel/config || mount -t configfs none /sys/kernel/config
if [ -d "$G" ]; then
    printf '' > "$G/UDC" || true
    rm -rf "$G"
fi

mkdir -p "$G"
printf '0x054c' > "$G/idVendor"
printf '0x0ce6' > "$G/idProduct"
printf '0x0200' > "$G/bcdUSB"
printf '0x0100' > "$G/bcdDevice"
mkdir -p "$G/strings/0x409"
printf '0001' > "$G/strings/0x409/serialnumber"
printf 'Sony' > "$G/strings/0x409/manufacturer"
printf 'DualSense Wireless Controller' > "$G/strings/0x409/product"
mkdir -p "$G/configs/c.1/strings/0x409"
printf 'DualSense' > "$G/configs/c.1/strings/0x409/configuration"
printf 250 > "$G/configs/c.1/MaxPower"
printf '0xe0' > "$G/configs/c.1/bmAttributes"
mkdir -p "$G/functions/hid.usb0"
printf 0 > "$G/functions/hid.usb0/subclass"
printf 0 > "$G/functions/hid.usb0/protocol"
printf 64 > "$G/functions/hid.usb0/report_length"
xxd -r -p "$HEX" > "$G/functions/hid.usb0/report_desc"
BYTES=$(wc -c < "$G/functions/hid.usb0/report_desc")
[ "$BYTES" -eq 337 ] || {
    echo "ds5-gadget-setup: report_desc is $BYTES bytes, expected 337" >&2
    exit 1
}
ln -s "$G/functions/hid.usb0" "$G/configs/c.1/hid.usb0"
UDC_NAME=$(ls /sys/class/udc | head -n1)
[ -n "$UDC_NAME" ] || { echo "No USB device controller found" >&2; exit 1; }
echo "$UDC_NAME" > "$G/UDC"
echo "ds5-gadget-setup: bound to $UDC_NAME, report_desc = $BYTES bytes"
