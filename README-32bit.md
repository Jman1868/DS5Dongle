# Raspberry Pi OS Legacy (32-bit) fallback

If the Raspberry Pi Zero 2 W 64-bit kernel does not keep the USB gadget
connected, install Raspberry Pi OS Legacy Lite (32-bit) with networking enabled.
Clone this repository and run `sudo ./install.sh`, then reboot. The installer
uses the same `dwc2` overlay and services on 32-bit Raspberry Pi OS.
