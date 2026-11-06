#!/bin/sh
set -eu

CONFIG=/boot/firmware/config.txt
CMDLINE=/boot/firmware/cmdline.txt
STAMP=$(date +%Y%m%d-%H%M%S)

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: run as root (sudo $0)" >&2
    exit 1
fi

backup_once() {
    file=$1
    backup="${file}.ds5dongle-pre-dwc2-${STAMP}.bak"
    cp -p "$file" "$backup"
    echo "BACKUP: $file -> $backup"
}

backup_once "$CONFIG"
backup_once "$CMDLINE"

if grep -qxF 'dtoverlay=dwc2' "$CONFIG"; then
    echo "UNCHANGED: dtoverlay=dwc2 already present in $CONFIG"
else
    printf '\n# DS5Dongle USB gadget\n dtoverlay=dwc2\n' | sed 's/^ //' >> "$CONFIG"
    echo "CHANGED: added dtoverlay=dwc2 to $CONFIG"
fi

if grep -qE '(^|[[:space:]])modules-load=[^[:space:]]*dwc2[^[:space:]]*,libcomposite([[:space:]]|$)' "$CMDLINE"; then
    echo "UNCHANGED: modules-load=dwc2,libcomposite already present in $CMDLINE"
else
    tmp=$(mktemp)
    awk 'BEGIN{done=0} {
        if (!done) { printf "%s modules-load=dwc2,libcomposite", $0; done=1 }
        else print
    }' "$CMDLINE" > "$tmp"
    cat "$tmp" > "$CMDLINE"
    rm -f "$tmp"
    echo "CHANGED: added modules-load=dwc2,libcomposite to $CMDLINE"
fi

echo "Reboot is required before the dwc2 UDC can be used."
