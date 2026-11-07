# DS5Dongle (Pi Zero 2 W port)

## What it is

This project ports [Jman1868/DS5Dongle](https://github.com/awalol/DS5Dongle)
from its original Raspberry Pi Pico 2 W firmware to the Raspberry Pi Zero 2 W.
A DualSense (PS5) controller pairs with the Pi over Bluetooth, while the Pi
presents itself to a host computer over USB as a wired DualSense
(`054c:0ce6`). The `ds5-bridge` userspace service forwards controller input and
host rumble reports between the Bluetooth and USB paths.

The host can be a PC, Steam Deck, or another system that supports a wired
DualSense. For architecture and implementation details, see
[CODEBASE.md](CODEBASE.md).

## Hardware you need

- Raspberry Pi Zero 2 W.
- A DualSense controller.
- A microSD card.
- A USB cable from the Pi to the host.

The Pi is powered **through the same micro-USB gadget port used for data**.
There is no separate `PWR IN` connection for this port setup. Do **not**
unplug the Pi to reset it: it will lose power and die mid-operation. Reboot it
with `sudo reboot` instead.

## Flashing and setup

1. Flash Raspberry Pi OS (64-bit), Debian **trixie**, to a microSD card.
2. Enable SSH by placing an empty file named `ssh` in the boot partition. For
   headless setup, also configure Wi-Fi with `wpa_supplicant.conf`, or use a
   serial/HDMI console.
3. Boot the Pi and SSH in as the default user.
4. Clone this repository and run the installer:

   ```sh
   sudo bash install.sh
   ```

   The installer installs dependencies, enables `dwc2` gadget mode through
   `config.txt` and `cmdline.txt`, builds with CMake, and installs the bridge
   binary, ConfigFS setup script, HID report descriptor, and systemd units. It
   enables both `ds5-gadget` and `ds5-bridge`.

5. Reboot:

   ```sh
   sudo reboot
   ```

After boot, connect the gadget port to the host. The Pi enumerates as a USB
DualSense (`054c:0ce6`) with no further host configuration.

## Pairing the controller

1. Hold **PS + Create** for about five seconds, until the light bar
   rapid-double-flashes.
2. Within about 30 seconds, the bridge discovers, pairs, and connects the
   controller. Follow progress with:

   ```sh
   journalctl -u ds5-bridge -f
   ```

   The log progresses through matching and connection, then reports a
   `hidraw` device such as `/dev/hidrawN`.
3. After the first pairing, the controller is bonded and should
   auto-reconnect.

## Using it

The controller works as a wired DualSense: buttons, sticks, triggers, D-pad,
and rumble are forwarded. Steam identifies it as **Sony DualSense Wireless
Controller**.

## Changes from the original repo

The original project is bare-metal Raspberry Pi Pico 2 W (RP2350) firmware
using TinyUSB and raw Bluetooth. This port replaces that firmware with a Linux
userspace bridge:

- USB gadget mode uses Linux ConfigFS and `f_hid`
  (`configfs/ds5-gadget-setup.sh`) with the 337-byte descriptor in
  `src/dualSense_hid_report.hex`.
- Bluetooth uses BlueZ and `sd-bus`; it does not shell out with
  `popen("busctl ...")`.
- `systemd/ds5-gadget.service` and `systemd/ds5-bridge.service` manage boot
  startup.
- `install.sh` provides one-shot setup.

The portable protocol layer,
[`portable/dualsense_protocol.h`](portable/dualsense_protocol.h) and
[`portable/dualsense_protocol.cpp`](portable/dualsense_protocol.cpp), is shared
with the Pico firmware.

The port also includes these protocol and reliability fixes:

- `f_hid` `GET_REPORT` requests are signaled by `POLLPRI`, not `POLLIN`.
- Bluetooth and USB input-report formats are translated instead of copied
  between incompatible layouts.
- `GET_REPORT` replies are pre-seeded so the host probe succeeds at boot.
- `GetManagedObjects` enumerates already-paired controllers for reconnect.
- `write_full()` handles `EAGAIN`/`EWOULDBLOCK` backpressure.
- Bluetooth output reports include the DualSense `0x31` format and CRC32
  required for rumble.

## Known limitations

After a Pi reboot while the controller is connected, the controller can
re-enter pairing mode. Its approximately 60-second limited-discoverable
window may expire before the bridge re-pairs it. Power-cycle the controller
(hold **PS** for 10 seconds, then press **PS**) to restore pairing.

## Troubleshooting

Check Pi services and whether the gadget device is open:

```sh
sudo systemctl status ds5-gadget ds5-bridge
sudo lsof /dev/hidg0
```

Check host enumeration and kernel messages:

```sh
sudo dmesg -T | grep -iE 'playstation|dualsense|054c'
lsusb | grep 054c
```

To re-enumerate the host probe without unplugging the cable, replace `N-M`
with the DualSense bus-port shown by `lsusb`:

```sh
echo 0 | sudo tee /sys/bus/usb/devices/N-M/authorized
sleep 2
echo 1 | sudo tee /sys/bus/usb/devices/N-M/authorized
```
