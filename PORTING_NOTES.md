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
- HID gadget scripts and a Linux probe are now present, but are untested on
  hardware until Pi and Windows results are collected.
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
- No behavior has been tested on a Pi Zero 2 W or Windows host.
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

## HID spike procedure

1. Run `enable-dwc2.sh` only after reviewing it; it backs up both boot files
   and does not change Wi-Fi or SSH settings.
2. Reboot, verify a UDC exists, then run `setup-gadget.sh`.
3. Build and run `ds5dongle_hid_probe`. It reports missing headers or ioctl
   failures explicitly. Feature seeds mirror the original firmware's cached
   report IDs and lengths (`src/main.cpp:154-191`, `src/bt.cpp:1009-1014`);
   payloads are zero until live controller data is available, so this is not
   a functional controller bridge.
