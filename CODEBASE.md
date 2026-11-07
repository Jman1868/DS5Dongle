# DS5Dongle architecture

This document describes the Raspberry Pi Zero 2 W Linux port. User setup and
operation instructions are in [README.md](README.md).

## Overview and data flow

```text
DualSense (Bluetooth)
        |
        v
   ds5-bridge
        |
        v
 /dev/hidg0 (f_hid) -> ConfigFS USB gadget -> host

host force feedback (FF)
        |
        v
   USB output report -> ds5-bridge -> Bluetooth output report -> controller
```

## Repository layout

- `src/ds5_bridge.cpp` — the userspace bridge; see the runtime threads and
  flows below.
- `portable/dualsense_protocol.{h,cpp}` — shared DualSense report
  parsing/formatting, also used by the Pico firmware.
- `src/dualSense_hid_report.hex` — the 337-byte HID report descriptor.
- `configfs/ds5-gadget-setup.sh` — builds the ConfigFS gadget, writes
  `report_desc`, and binds the UDC (`3f980000.usb` on the Pi Zero 2 W).
- `systemd/ds5-gadget.service`, `systemd/ds5-bridge.service` — boot ordering;
  the bridge runs before the gadget so it can seed replies before the host
  probe.
- `install.sh` — installs dependencies, build output, gadget files, and
  services for one-shot setup.
- `CMakeLists.txt` — defines the C++17 `ds5-bridge` build and its BlueZ,
  libusbgx, and systemd dependencies.

## Runtime architecture

### Startup and discovery

`main()` opens `/dev/hidg0`, retrying until the gadget is present. It then
seeds `GET_REPORT` replies with `userspace_req = 0` for report IDs `0x09`,
`0x20`, and `0x05`, starts the `get_report_servo` thread, and sets up BlueZ:
the adapter is powered and made pairable, the `org.bluez.Agent1` agent is
registered, and discovery begins.

Discovery first calls `enumerate_existing()` through
`ObjectManager.GetManagedObjects` so an already-paired controller can
reconnect. If none matches, `InterfacesAdded` and `PropertiesChanged` signals
are used during discovery. `pair_connect()` handles pairing, connection, and
the already-existing bond case. If a stale limited-discoverable bond cannot
connect, `remove_device()` removes it so the next discovery can rebuild the
bond.

### GET_REPORT and host output thread

`get_report_servo()` polls `/dev/hidg0` for `POLLIN | POLLPRI`:

- `POLLPRI` means the host requested `GET_REPORT`. The thread reads the
  requested ID with `GADGET_HID_READ_GET_REPORT_ID`, builds the response with
  `fill_report()`, and answers with `GADGET_HID_WRITE_GET_REPORT`.
- `POLLIN` means a host output report is ready. It reads the report and queues
  it in `HidState::output_reports` for `pump()`.

`fill_report()` constructs the essential host probe replies: `0x09` pairing
information, `0x20` firmware information, and `0x05` calibration data.

### Report pump

`pump()` reads Bluetooth input reports from `hidraw` (normally report `0x31`),
converts them to the USB input report `0x01` with `make_usb_input_report()`,
and writes the fixed 64-byte report to `/dev/hidg0`. Host USB output reports
(`0x02`) are converted by `make_bluetooth_output()` to Bluetooth output
reports (`0x31` plus CRC32) and sent to the controller. `write_full()` handles
short writes and waits through `EAGAIN`/`EWOULDBLOCK` backpressure.

If no matching `hidraw` device appears, `main()` falls back to the Bluetooth
L2CAP control and interrupt channels.

## Report formats

The format details correspond to the Linux PlayStation HID driver,
[`drivers/hid/hid-playstation.c`](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c):

- Linux `f_hid` signals `GET_REPORT` with `POLLPRI`. It is answered with
  `GADGET_HID_READ_GET_REPORT_ID` followed by
  `GADGET_HID_WRITE_GET_REPORT`; it is not answered with `read()`.
- A Bluetooth input report `0x31` is 78 bytes, while a USB input report `0x01`
  is 64 bytes. Their layouts differ, so the bridge parses/rebuilds the report
  rather than copying it.
- A Bluetooth output report `0x31` is 78 bytes: report ID, sequence tag,
  `0x10`, 47 bytes of common output data, 24 reserved bytes, and CRC32.
  `output_crc()` computes standard CRC32 with initial value `0xFFFFFFFF` and
  final XOR. The checksum covers `[0xA2] + report[0..73]` and is stored
  little-endian at `report[74..77]`.

The portable layer exposes `ds5::parse_bluetooth_input()` and
`ds5::make_bluetooth_output()` for the shared report representation. The
Linux bridge additionally handles the transport framing and the USB-specific
layout in `src/ds5_bridge.cpp`.

## Build and test

Build the bridge with:

```sh
cmake -B build
cmake --build build
```

On a Pi, `install.sh` performs the dependency installation, build, gadget
installation, and service enablement:

```sh
sudo bash install.sh
```

For input testing on the host, identify the event device and run:

```sh
sudo evtest
```

Select the Sony DualSense event device and press buttons, move sticks, and
operate triggers. For the force-feedback path, use a force-feedback test
utility such as `fftest /dev/input/eventN`, or a game that requests rumble.
The host's USB `0x02` output report is queued by `get_report_servo()`,
translated by `make_bluetooth_output()`, and delivered to the controller with
the Bluetooth `0x31` framing and CRC32.
