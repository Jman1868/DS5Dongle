# DS5Dongle on Raspberry Pi Zero 2 W

This branch replaces the Pico firmware transport with a Linux USB gadget and
BlueZ L2CAP bridge. It supports the DualSense (DS5) only; DS4 is not supported.

## Install

Use Raspberry Pi OS Lite (Bookworm, 64-bit), connect the Pi to the network,
clone this repository, and run:

```sh
sudo ./install.sh
sudo reboot
```

The Pi **must be powered through PWR IN**. The gadget USB port is host-facing
and cannot provide enough power for the Pi Zero 2 W.

Put the controller into pairing mode by holding **Create + PS** until the light
bar flashes. BlueZ discovers, pairs, trusts, connects, and reconnects it
automatically.

## Verify

On the Pi:

```sh
systemctl status ds5-gadget ds5-bridge
```

Both services should be active. On the host PC, the gadget should appear as:

```text
054c:0ce6 Sony Corp. DualSense Wireless Controller
```

Use `evtest` or `jstest` on the host and press controller buttons to verify
input reports. This initial port does not implement UAC2 audio, so no USB audio
device is expected.

## Troubleshooting

Inspect bridge logs with `journalctl -u ds5-bridge -f`. If pairing is unreliable
because of Wi-Fi/Bluetooth coexistence, uncomment the optional
`dtoverlay=disable-wifi` line in `install.sh` and reinstall/reboot.

If gadget mode is unstable with the 64-bit kernel, use Raspberry Pi OS Legacy
(32-bit), then run the same installation steps; see [README-32bit.md](README-32bit.md).
