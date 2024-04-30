#!/usr/bin/env python3

import sys
import usb.core
import usb.util

VID = 0xF055
PID = 0x9999

EP_IN = 0x81  # in to host
EP_OUT = 0x01  # out from host


def main():
    dev = usb.core.find(idVendor=VID, idProduct=PID)

    if dev is None:
        print("No USB device found")
        sys.exit(1)

    # dev.set_configuration()

    usb.util.claim_interface(dev, 0)

    # Read the device's strings.
    for i in range(0x11, 0x16):
        print(f"str{i}:", usb.util.get_string(dev, i))

    # Test writing to the device.
    ret = dev.write(EP_OUT, b"01234567", timeout=1000)
    print(ret)

    print(dev.read(EP_IN, 64))

    usb.util.release_interface(dev, 0)
    usb.util.dispose_resources(dev)


if __name__ == "__main__":
    main()
