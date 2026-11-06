#!/bin/sh
set -eu

ROOT=/sys/kernel/config/usb_gadget
NAME=ds5dongle
G="$ROOT/$NAME"
CONFIG_FILE=${DS5DONGLE_CONFIG:-/etc/ds5dongle/gadget.conf}
SOURCE=${DS5DONGLE_SOURCE:-$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)

VID=0x054c
PID=0x0ce6
MANUFACTURER='Sony Interactive Entertainment'
PRODUCT='DualSense Wireless Controller'
EDGE_REPORT=0

if [ -r "$CONFIG_FILE" ]; then
    # This file is root-owned configuration, not host input.
    . "$CONFIG_FILE"
fi
normalize_hex() {
    value=$(printf '%s' "$1" | sed 's/^0[xX]//')
    case "$value" in
        ????)
            case "$value" in
                *[!0-9a-fA-F]*) echo "ERROR: VID/PID must be exactly four hexadecimal digits, with an optional 0x prefix: $1" >&2; exit 1;;
            esac
            ;;
        *) echo "ERROR: VID/PID must be exactly four hexadecimal digits, with an optional 0x prefix: $1" >&2; exit 1;;
    esac
    printf '0x%04x' "0x$value"
}
VID=$(normalize_hex "$VID")
PID=$(normalize_hex "$PID")
case "$EDGE_REPORT" in 0|1) ;; *) echo "ERROR: EDGE_REPORT must be 0 or 1" >&2; exit 1;; esac

if [ "$(id -u)" -ne 0 ]; then echo "ERROR: run as root (sudo $0)" >&2; exit 1; fi
if [ ! -d "$ROOT" ]; then echo "ERROR: ConfigFS is not mounted at $ROOT" >&2; exit 1; fi
if [ -d "$G" ]; then
    "$SCRIPT_DIR/teardown-gadget.sh"
fi
UDC=$(ls -1 /sys/class/udc 2>/dev/null | head -n 1 || true)
if [ -z "$UDC" ]; then echo "ERROR: no UDC found; run enable-dwc2.sh and reboot first" >&2; exit 1; fi

report_source=desc_hid_report_ds
report_length=337
product_default='DualSense Wireless Controller'
pid_default=0x0ce6
if [ "$EDGE_REPORT" -eq 1 ]; then
    report_source=desc_hid_report_dse
    report_length=453
    product_default='DualSense Edge Wireless Controller'
    pid_default=0x0df2
fi
[ "$PID" = 0x0ce6 ] && PID=$pid_default
[ "${PRODUCT:-}" = 'DualSense Wireless Controller' ] && PRODUCT=$product_default

mkdir -p /etc/ds5dongle /var/lib/ds5dongle
serial_file=/var/lib/ds5dongle/serial
if [ ! -s "$serial_file" ]; then
    seed=$(cat /etc/machine-id 2>/dev/null || hostname)
    printf 'DS5D-%s\n' "$(printf '%s' "$seed" | sha256sum | cut -c1-16)" > "$serial_file"
    chmod 600 "$serial_file"
fi
SERIAL=$(tr -d '\r\n' < "$serial_file")

mkdir -p "$G"
printf '%s' "$VID" > "$G/idVendor"
printf '%s' "$PID" > "$G/idProduct"
printf '%s' 0x0200 > "$G/bcdUSB"
printf '%s' 0x0100 > "$G/bcdDevice"
mkdir -p "$G/strings/0x409"
printf '%s' "$MANUFACTURER" > "$G/strings/0x409/manufacturer"
printf '%s' "$PRODUCT" > "$G/strings/0x409/product"
printf '%s' "$SERIAL" > "$G/strings/0x409/serialnumber"
mkdir -p "$G/configs/c.1/strings/0x409"
printf '%s' 'DualSense HID' > "$G/configs/c.1/strings/0x409/configuration"
printf '%s' 250 > "$G/configs/c.1/MaxPower"
mkdir -p "$G/functions/hid.usb0"
printf '%s' 0 > "$G/functions/hid.usb0/subclass"
printf '%s' 0 > "$G/functions/hid.usb0/protocol"
printf '%s' 64 > "$G/functions/hid.usb0/report_length"

python3 "$SCRIPT_DIR/parse_hid_descriptor.py" \
    "$SOURCE/src/usb_descriptors.cpp" "$report_source" \
    "$G/functions/hid.usb0/report_desc"
actual_length=$(wc -c < "$G/functions/hid.usb0/report_desc")
if [ "$actual_length" -ne "$report_length" ]; then
    echo "ERROR: descriptor $report_source: expected $report_length bytes, found $actual_length" >&2
    exit 1
fi

ln -s "$G/functions/hid.usb0" "$G/configs/c.1/hid.usb0"
printf '%s' "$UDC" > "$G/UDC"
echo "ENABLED: $G on UDC $UDC"
echo "IDENTITY: VID=$VID PID=$PID manufacturer=$MANUFACTURER product=$PRODUCT serial=$SERIAL"
echo "SOURCE: $report_source in src/usb_descriptors.cpp (DS line 513; Edge line 684)"
