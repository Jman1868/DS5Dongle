# Linux porting notes

This file records behavior differences and unresolved questions for the
Raspberry Pi Zero 2 W port. The existing Pico firmware remains the reference
implementation.

## Confirmed decisions

- USB identity is preserved: Sony VID `0x054C`, DualSense PID `0x0CE6`, and
  DualSense Edge PID `0x0DF2`.
- Bluetooth pairing is delegated to BlueZ.
- The first report transport implementation will use BlueZ/kernel `hidraw`.
  Raw L2CAP sockets are the fallback if hidraw cannot provide the required
  feature-report or output-report behavior.
- Linux-specific code is being added under `linux/` and `portable/`; the Pico
  build remains separate.

## Behavior differences

- USB device mode will use the Pi Zero 2 W micro-USB DATA port and the `dwc2`
  gadget controller, not Pico USB hardware.
- HID-only stage uses ConfigFS `f_hid`; audio remains disabled pending hardware
  enumeration.
- BlueZ and the kernel HCI stack replace BTstack and CYW43 polling.
- `/etc/ds5dongle/` and `/var/lib/ds5dongle/` replace Pico flash settings and
  BTstack TLV storage.
- BOOTSEL and reboot-to-BOOTSEL are not available on the Pi.
- Pico RAM relocation and RP2350 multicore flash-safety code are not needed.
- The ACT LED or a configured GPIO replaces the Pico CYW43 LED.

## Known limitations

- The Linux daemon does not yet pair with or bridge a controller.
- HID gadget scripts and a Linux probe are present. On Linux/Zorin with xHCI,
  the gadget enumerated cleanly at high speed as VID `054C`, PID `0CE6`, with
  the Sony strings and generated serial; `hid-playstation` bound and created
  `hidraw0`. This is observed hardware behavior, not a Windows result.
- The default probe returns zero-length GET_REPORT responses, so
  `hid-playstation` rejects the device while probing feature report `0x09`.
  The probe has an explicit, loud `--spike-fake-features` mode for synthetic
  reports; it is never enabled by default and remains untested on hardware.
- With synthetic features enabled, Linux/Zorin kernel `hid-playstation`
  registered the controller successfully with fake hardware/firmware versions,
  gamepad, motion, touchpad, and headset-jack input devices plus by-id event
  and js nodes. The host requested report IDs `0x09` (20 bytes), `0x20`
  (64 bytes), and `0x05` (41 bytes); all payloads were synthetic.
- The host fully registered the synthetic device with gamepad, motion,
  touchpad, and headset-jack inputs plus by-id nodes. This is observed on
  hardware, but all feature payloads were synthetic.
- The host's `evtest` showed the complete DualSense capability set (buttons,
  six axes, hat, and `FF_RUMBLE`) but no events with an unchanged static input
  report. The probe now has opt-in `--spike-animate` mode, clearly labeled
  `SPIKE ONLY`, which changes the X axis and toggles one button once per
  second. It is disabled by default and is intended only to prove input
  delivery.
- Teardown is observed to be idempotent: with the gadget already absent,
  `tools/pi/teardown-gadget.sh` reports `UNCHANGED: gadget is not configured`.
- The repeated GET_REPORT `0x05` burst is unexplained; revisit it with the
  fixed probe on a clean connect/disconnect. Do not infer its cause from the
  earlier static probe behavior.
- `f_hid` signals a new GET_REPORT through `POLLPRI`; the probe now waits for
  that event. A transport `ESHUTDOWN`/`ENODEV` now produces one rate-limited
  message and clean exit.
- The source descriptor authority is `src/usb_descriptors.cpp`: the DS HID
  report array starts at line 513 and is asserted to 337 bytes at line 682;
  the Edge array starts at line 684 and is asserted to 453 bytes at line 911.
  The HID interface descriptor is at line 379. The setup script extracts those
  exact arrays; it does not invent report bytes.
- The original firmware audio descriptors are UAC1 at lines 178-377:
  playback is 4-channel, 48 kHz, 16-bit, and capture is 2-channel, 48 kHz,
  16-bit. Stock ConfigFS `uac1` can select channel counts, sample rates, and
  sample sizes, but cannot reproduce the original interface numbers, terminal
  graph, endpoint numbers, exact packet sizes, or all endpoint attributes.
  This is an analysis only; audio is not implemented.
- Pi Zero 2 W behavior and Windows behavior remain untested. An earlier
  transient Linux attach failure (`error -62`) was observed before a later
  clean enumeration; its cause is unknown and may be cable/timing related.
- The compatibility of `hidraw` with the DualSense Bluetooth feature-report
  protocol remains unverified.
- Windows enumeration remains untested.
- USB output reports for rumble, lightbar, and haptics remain untested.
- Audio remains untested: stock UAC1 matching versus a later FunctionFS
  implementation has not been exercised on hardware.

## Open questions

- Whether the target Bookworm kernel's FunctionFS implementation can sustain
  the required full-speed isochronous audio endpoints without underruns.
- Whether kernel HID transport exposes the Bluetooth `0xA2`/`0xA3` framing
  unchanged through `hidraw`.
- Whether raw L2CAP can coexist with BlueZ's HID profile without disabling or
  replacing the profile.
- Which Pi GPIO and LED should be the default for status indication.
- Whether the installed 6.18.50+rpt-rpi-v8 kernel ships
  `linux/usb/g_hid.h` and implements the GET_REPORT ioctls at runtime.
- Whether Windows accepts the HID-only Sony identity and report descriptor.
- Whether synthetic feature reports allow `hid-playstation` to register input
  devices and expose the probe's static input reports.
- Input report delivery of Cross and changing left-stick X remains unverified
  on hardware until the animated probe is observed with `evtest`.

## HID spike procedure

1. Run `enable-dwc2.sh` only after reviewing it; it backs up both boot files
   and does not change Wi-Fi or SSH settings.
2. Reboot, verify a UDC exists, then run `setup-gadget.sh`.
3. Build and run `ds5dongle_hid_probe`. It reports missing headers or ioctl
failures explicitly. Use no flag for the default zero-length behavior; use
`--spike-fake-features` only for the synthetic feature experiment. The
synthetic payload layouts follow kernel `hid-playstation.c` v6.18:
pairing `0x09`/20 bytes, firmware `0x20`/64 bytes, calibration `0x05`/41
bytes. These values are deliberately fake and are not a controller bridge.

## Proposed Phase 3.3: BlueZ and hidraw bridge

1. Pair the real DualSense through BlueZ and identify its `hidraw` node from
the udev/HID metadata without assuming a fixed `/dev/hidrawN` number.
2. Open the controller's hidraw device with the required permissions and
verify feature-report framing and report IDs using `HIDIOCGRAWINFO`,
`HIDIOCGFEATURE`, and `HIDIOCSFEATURE`.
3. Replace the spike probe's synthetic GET_REPORT responses with synchronous
forwarding to the real controller, preserving the requested report ID and
length. Cache only reports that are known safe to cache; pairing,
firmware, and calibration should be fetched and validated before the USB
gadget is exposed.
4. Forward host SET_REPORT output reports to the controller's hidraw path,
retaining the USB report ID and rejecting oversized or unsupported reports
explicitly.
5. Add reconnect/error handling for BlueZ disconnects and hidraw removal, then
validate with the kernel `hid-playstation` probe and Windows HID clients.

The main open implementation question is whether BlueZ's kernel HID profile
already owns the relevant transport in a way that permits complete feature
report access through hidraw. Raw L2CAP remains a fallback, not part of this
phase's initial implementation.

Specific Phase 3.3 risks are:

- `hid-playstation` may claim the Bluetooth HID device on the Pi before the
  bridge opens it, changing which hidraw node is available or consuming
  feature requests.
- BlueZ/kernel hidraw may expose only parsed HID reports, reject feature
  reports, or hide the Bluetooth `0x43`/`0xA3` and `0x53`/`0xA2` framing.
- Bluetooth feature responses require correct CRC32 and sequence handling;
  forwarding USB payloads without translating the transport framing will fail.
- Synchronous USB GET_REPORT handling can exceed the host's control-transfer
  timeout if the controller is asleep, disconnected, or slow to answer.
- A reconnect race can leave stale hidraw descriptors or a pending ConfigFS
  control request; every failure path must release and rediscover both ends.

### Phase 3.3 test checklist

1. Pair a real controller with BlueZ while the USB gadget is stopped; record
   `bluetoothctl devices`, `bluetoothctl info <address>`, and the matching
   `/dev/hidraw*` udev properties.
2. Confirm whether `hid-playstation` claims the controller on the Pi. Record
   `dmesg`, `/sys/bus/hid/devices`, and all `hidraw` symlinks. If it claims
   the device, decide whether the bridge must use its hidraw node or prevent
   competing binding before proceeding.
3. Use a standalone hidraw feature-report test to request `0x09` (20 bytes),
   `0x20` (64 bytes), and `0x05` (41 bytes), recording return lengths, report
   IDs, and whether Bluetooth framing/CRC is exposed or handled by the kernel.
4. With the USB gadget attached to a Linux host, forward those three requests
   and compare host-visible lengths and IDs with the direct hidraw results.
5. Repeat with the Windows HID host and capture the probe log, Device Manager,
   and USB descriptor viewer results.
6. Only after feature forwarding is stable, test SET_REPORT output forwarding
   separately for rumble, lightbar, and haptics, with a safe teardown path.
7. Test controller disconnect/reconnect, USB host disconnect/reconnect, and
   simultaneous Wi-Fi SSH access. Preserve logs from both sides.

For the input-delivery spike, run:

```sh
timeout 30s sudo ./build-linux/ds5dongle_hid_probe /dev/hidg0 --spike-animate
sudo evtest "$(find /dev/input/by-id -maxdepth 1 -type l -name '*event-joystick' -print -quit)"
```

Expect `ABS_X` values `0, 64, 128, 192, 255` cycling once per second and
one mapped button alternating between key values 1 and 0. No events from the
unchanged default report are expected; evdev reports capabilities separately
from value-change events.

## Hardware observations and kernel probe order

The v6.18 `hid-playstation` source requests features in this order for a
DualSense: pairing information `0x09` (20 bytes), firmware information `0x20`
(64 bytes), then calibration `0x05` (41 bytes). Each request requires the
exact byte count and `buf[0]` to equal the requested report ID. USB does not
perform the Bluetooth CRC check. The pairing response copies bytes 1-6 as the
MAC; firmware reads little-endian values at offsets 24, 28, and 44; calibration
reads signed little-endian fields at offsets 1-33 and replaces zero
normalization denominators with safe defaults. Source reference:
`drivers/hid/hid-playstation.c`, `ps_get_report` around lines 788-810,
`dualsense_get_mac_address` around 1167-1185,
`dualsense_get_firmware_info` around 1137-1163,
and `dualsense_get_calibration_data` around 1007-1132.

The initial host log was reported as `0x05`, `0x09`, `0x20`, but the repeated
`0x05` entries were not reliable evidence of new host requests: the probe was
re-reading a stale report ID because it did not wait for `POLLPRI`. The
driver's source order is authoritative for the normal registration path:
`0x09` -> `0x20` -> `0x05`. Future logs from the fixed probe should be used
to confirm the on-wire order.
