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
- ConfigFS/FunctionFS replaces TinyUSB.
- BlueZ and the kernel HCI stack replace BTstack and CYW43 polling.
- `/etc/ds5dongle/` and `/var/lib/ds5dongle/` replace Pico flash settings and
  BTstack TLV storage.
- BOOTSEL and reboot-to-BOOTSEL are not available on the Pi.
- Pico RAM relocation and RP2350 multicore flash-safety code are not needed.
- The ACT LED or a configured GPIO replaces the Pico CYW43 LED.

## Known limitations

- The Linux daemon does not yet pair with or bridge a controller.
- USB gadget descriptors and FunctionFS endpoint handling are not implemented.
- Audio is not implemented.
- No behavior has been tested on a Pi Zero 2 W or with a physical DualSense.
- The compatibility of stock kernel UAC functions with the original UAC1
  four-channel/two-channel descriptors remains unverified.
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
