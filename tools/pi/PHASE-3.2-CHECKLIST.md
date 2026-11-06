# Phase 3.2 HID-only checklist

The boot script is intentionally a separate approval gate. Do not run it
until its contents have been reviewed.

## Pi commands (paste exact output)

1. Review the script:

   ```sh
   sed -n '1,240p' tools/pi/enable-dwc2.sh
   ```

2. Before changing boot files, capture state:

   ```sh
   printf '%s\n' '--- model ---'; cat /proc/device-tree/model
   printf '%s\n' '--- kernel ---'; uname -r
   printf '%s\n' '--- config ---'; grep -nE 'dwc2|modules-load' /boot/firmware/config.txt /boot/firmware/cmdline.txt || true
   printf '%s\n' '--- udc ---'; ls -la /sys/class/udc
   printf '%s\n' '--- headers ---'; grep -R -nE 'GADGET_HID_(READ_GET_REPORT_ID|WRITE_GET_REPORT)|struct usb_hidg_report' /usr/src/linux-headers-$(uname -r) /usr/include/linux/usb/g_hid.h 2>&1 || true
   ```

3. After approval, enable dwc2 and paste output:

   ```sh
   sudo tools/pi/enable-dwc2.sh
   ```

4. Reboot only after confirming Wi-Fi SSH can be re-established:

   ```sh
   sudo reboot
   ```

5. After SSH reconnect, paste:

   ```sh
   uname -r
   ls -la /sys/class/udc
   lsmod | grep -E 'dwc2|libcomposite|usb_f_hid' || true
   ```

6. Build the probe on the Pi and paste output:

   ```sh
   cmake -S linux -B build-linux
   cmake --build build-linux --target ds5dongle_hid_probe
   ```

7. Configure and enable HID, then paste output:

   ```sh
   sudo tools/pi/setup-gadget.sh
   ls -l /dev/hidg0
   cat /sys/kernel/config/usb_gadget/ds5dongle/UDC
   ```

8. Run the probe for 30 seconds and paste all output:

   ```sh
   timeout 30s sudo ./build-linux/ds5dongle_hid_probe /dev/hidg0
   ```

   Repeat the same test for the opt-in synthetic feature experiment:

   ```sh
   timeout 30s sudo ./build-linux/ds5dongle_hid_probe /dev/hidg0 --spike-fake-features
   ```

   The second command must print the `SPIKE ONLY` banner. It is not a
   controller implementation and must never be used as the default service.

   To make evdev changes visible during the HID input test, run:

   ```sh
   timeout 30s sudo ./build-linux/ds5dongle_hid_probe /dev/hidg0 --spike-animate
   ```

   This prints a second `SPIKE ONLY` banner and changes the X axis plus one
   button once per second. It is disabled by default.

9. Recover/teardown and paste output:

   ```sh
   sudo tools/pi/teardown-gadget.sh
   test ! -e /sys/kernel/config/usb_gadget/ds5dongle && echo 'teardown complete'
   ```

If SSH is lost, unplug the USB data cable from the Windows host first. The
gadget scripts do not alter Wi-Fi or SSH configuration. If boot changes must be
reverted locally, use the timestamped `.ds5dongle-pre-dwc2-*.bak` files printed
by the enable script, then reboot; do not delete the only backup.

## Windows checks (paste observations/screenshots or exported reports)

1. Connect the Pi Zero 2 W center DATA port to the Windows target host.
2. In Device Manager, record the new device name, hardware IDs (including
   `USB\VID_054C&PID_0CE6`), driver status, and any warning code.
3. In USB Device Tree Viewer (or USBView), record device/configuration/interface
   descriptors, HID report descriptor length, endpoint addresses, and whether
   GET_DESCRIPTOR/GET_REPORT requests complete.
4. Open `joy.cpl`, select the controller if present, and record its name,
   button/axis response, and whether the test page remains responsive.
5. In Windows Sound settings, record that no audio device is expected in this
   HID-only stage.
6. Paste the probe log showing every GET_REPORT and SET_REPORT request. Do not
   proceed to audio work until these HID results are reviewed.

## Linux-host follow-up for the fake-feature experiment

When testing against a Linux host with `hid-playstation`, record:

1. `dmesg` lines showing whether pairing, firmware, and calibration requests
   pass.
2. `ls -l /dev/hidraw*` and the matching `evtest` device list.
3. `jstest --list` and button/axis output, if `jstest` is installed.
4. Whether the static report from the probe appears in `evtest` or `jstest`.

For the animated input test, identify the joystick event node and run this
exact command:

```sh
sudo evtest "$(find /dev/input/by-id -maxdepth 1 -type l -name '*event-joystick' -print -quit)"
```

Expected output includes `Event: time=... type 3 (EV_ABS), code 0 (ABS_X),
value ...` with values cycling through `0`, `64`, `128`, `192`, `255`, and
`Event: time=... type 1 (EV_KEY), code ... value 1` followed about a second
later by the same key with `value 0`. The exact button code depends on the
kernel's DualSense mapping. The non-animated probe can show capabilities but
will normally produce no evdev events because its report values never change.

## Fresh animated-input verification

On the Pi, from the repository root, paste the complete output of:

```sh
sudo tools/pi/teardown-gadget.sh
rm -rf build-linux
cmake -S linux -B build-linux
cmake --build build-linux --target ds5dongle_hid_probe
sudo tools/pi/setup-gadget.sh
sudo ./build-linux/ds5dongle_hid_probe /dev/hidg0 --spike-animate
```

Leave the final command running. On the host, paste the first approximately
20 event lines from the event node resolved by:

```sh
EVENT=$(find /dev/input/by-id -maxdepth 1 -type l -name '*event-joystick' -print -quit)
printf 'EVENT_NODE=%s\n' "$EVENT"
sudo evtest "$EVENT"
```

The pasted host output must include `EV_KEY` `BTN_SOUTH` values alternating
between `1` and `0`, and `EV_ABS` `ABS_X` values changing through the
`0, 64, 128, 192, 255, 0` sequence. Do not claim input delivery until those
lines are present. After stopping the Pi probe with Ctrl+C, paste:

```sh
sudo sh tools/pi/teardown-gadget.sh
ls -l /dev/hidg0 2>&1 || true
```
