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
