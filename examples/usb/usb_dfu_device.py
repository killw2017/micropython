# Implementation of USB DFU device in Python.
# TODO:
# - use usb_rx_buf also for TX
# - get it working on Pico without having to enable UART REPL
# - add more comments
# - document how to run it on a Pico

import struct, machine

# USB constants for bmRequestType.
USB_REQ_RECIP_INTERFACE = 0x01
USB_REQ_TYPE_CLASS = 0x20
USB_DIR_OUT = 0x00
USB_DIR_IN = 0x80

# String describing the memory layout of the DFU device.
MEMORY_LAYOUT = b"@Internal Flash  /0x08000000/16*128Kg"

# VID and PID of the DFU device (these are the ST values).
VID = 0x0483
PID = 0xdf11

_desc_dev = bytes([
    0x12,  # bLength
    0x01,  # bDescriptorType: Device
    0x00, 0x02,  # USB version: 2.00
    0x00,  # bDeviceClass
    0x00,  # bDeviceSubClass
    0x00,  # bDeviceProtocol
    0x40,  # bMaxPacketSize
    VID & 0xff, VID >> 8 & 0xff,  # VID
    PID & 0xff, PID >> 8 & 0xff,  # PID
    0x00, 0x01,  # bcdDevice: 1.00
    0x11,  # iManufacturer
    0x12,  # iProduct
    0x13,  # iSerialNumber
    0x01,  # bNumConfigurations: 1
])

_desc_cfg = bytes([
    # Configuration Descriptor.
    0x09,  # bLength
    0x02,  # bDescriptorType
    0x1B, 0x00,  # wTotalLength: 27
    0x01,  # bNumInterfaces
    0x01,  # bConfigurationValue
    0x00,  # iConfiguration
    0x80,  # bmAttributes (bus powered)
    0x32,  # bMaxPower
    # Interface Descriptor.
    0x09,  # bLength
    0x04,  # bDescriptorType
    0x00,  # bInterfaceNumber
    0x00,  # bNumEndpointns
    0x00,  # bAlternateSetting
    0xFE,  # bInterfaceClass: application specific interface
    0x01,  # bInterfaceSubClasse: device firmware update
    0x02,  # bInterfaceProtocol
    0x14,  # iInterface
    # Device Firmware Upgrade Interface Descriptor.
    0x09,  # bLength
    0x21,  # bDescriptorType
    0x0B,  # bmAttributes (will detach, upload supported, download supported)
    0xFF, 0x00,  # wDetatchTimeout
    0x00, 0x08,  # wTransferSize
    0x1A, 0x01,  # bcdDFUVersion
])

_desc_strs = {
    0x11: b"iManufacturer",
    0x12: b"iProduct",
    0x13: b"iSerialNumber",
    0x14: MEMORY_LAYOUT,
}

# USB buffer for transfers.
usb_rx_buf = bytearray(2048) # corresponds to wTransferSize in _desc_cfg

def _open_itf_cb(interface_desc_view):
    print("_open_itf_cb", bytes(interface_desc_view))
    # prepare to receive first data packet on the OUT endpoint
    #usbd.submit_xfer(EP_OUT, buf_out)


def _reset_cb():
    print("_reset_cb")

def _control_xfer_cb(stage, request):
    bmRequestType, bRequest, wValue, wIndex, wLength = struct.unpack("<BBHHH", request)
    #print("_control_xfer_cb", stage, bmRequestType, bRequest, wValue, wIndex, wLength)
    if stage == 1: # SETUP
        if bmRequestType == USB_DIR_OUT | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE:
            # Data coming from host, prepare to receive it.
            return memoryview(usb_rx_buf)[:wLength]
        if bmRequestType == USB_DIR_IN | USB_REQ_TYPE_CLASS | USB_REQ_RECIP_INTERFACE:
            # Host requests data, prepare to send it.
            return dfu.handle_tx(bRequest, wValue, wLength)
    elif stage == 3: # ACK
        if bmRequestType & USB_DIR_IN:
            # EP0 TX sent.
            dfu.process()
        else:
            # EP0 RX ready.
            dfu.handle_rx(bRequest, wValue, wLength)
    return True


class DFU:
    # DFU class requests
    DETACH = 0
    DNLOAD = 1
    UPLOAD = 2
    GETSTATUS = 3
    CLRSTATUS = 4
    GETSTATE = 5
    ABORT = 6

    # DFU States
    STATE_IDLE = 2
    STATE_BUSY = 4
    STATE_DNLOAD_IDLE = 5
    STATE_MANIFEST = 7
    STATE_UPLOAD_IDLE = 9
    STATE_ERROR = 0xA

    # Commands
    CMD_NONE = 0
    CMD_EXIT = 1
    CMD_UPLOAD = 7
    CMD_DNLOAD = 8

    #
    CMD_DNLOAD_SET_ADDRESS = 0x21
    CMD_DNLOAD_ERASE = 0x41
    CMD_DNLOAD_READ_UNPROTECT = 0x92

    # Error status flags
    STATUS_OK = 0x00

    def __init__(self):
        self.state = DFU.STATE_IDLE
        self.cmd = DFU.CMD_NONE
        self.status = DFU.STATUS_OK
        self.error = 0
        self.leave_dfu = False
        self.addr = 0

    def handle_rx(self, cmd, arg, len):
        if cmd == DFU.CLRSTATUS:
            self.state = DFU.STATE_IDLE
            self.cmd = DFU.CMD_NONE
            self.status = DFU.STATUS_OK
            self.error = 0
        elif cmd == DFU.ABORT:
            self.state = DFU.STATE_IDLE
            self.cmd = DFU.CMD_NONE
            self.status = DFU.STATUS_OK
            self.error = 0
        elif cmd == DFU.DNLOAD:
            if len == 0:
                # exit DFU
                self.cmd = DFU.CMD_EXIT
            else:
                # download
                self.cmd = DFU.CMD_DNLOAD;
                #self.wBlockNum = arg;
                #self.wLength = len;
                #memcpy(self.buf, usb_rx_buf, len)

    def handle_tx(self, cmd, arg, max_len):
        if cmd == DFU.UPLOAD:
            if arg >= 2:
                self.cmd = DFU.CMD_UPLOAD
                addr = (arg - 2) * max_len + self.addr
                #self.do_read(self.addr, max_len)
                return b"a" * max_len
            return None
        elif cmd == DFU.GETSTATUS and max_len == 6:
            if self.cmd == DFU.CMD_NONE:
                pass
            elif self.cmd == DFU.CMD_EXIT:
                self.state = DFU.STATE_MANIFEST
            elif self.cmd == DFU.CMD_UPLOAD:
                self.state = DFU.STATE_UPLOAD_IDLE
            elif self.cmd == DFU.CMD_DNLOAD:
                self.state = DFU.STATE_BUSY
            else:
                self.state = DFU.STATE_BUSY

            buf = bytes([self.status, 0, 0, 0, self.state, self.error])

            # clear errors now they've been sent to host
            self.status = DFU.STATUS_OK
            self.error = 0

            return buf
        else:
            return None

    def process(self):
        #print('process')
        if self.state == DFU.STATE_MANIFEST:
            #print('manifest')
            self.leave_dfu = True
        elif self.state == DFU.STATE_BUSY:
            if self.cmd == DFU.CMD_DNLOAD:
                #print('dnload')
                self.cmd = DFU.CMD_NONE
                self.state = self.process_dnload()

    def process_dnload(self):
        # TODO
        return DFU.STATE_DNLOAD_IDLE

dfu = DFU()

usbd = machine.USBDevice()
usbd.active(0)
usbd.builtin_driver = usbd.BUILTIN_NONE
usbd.config(
    desc_dev=_desc_dev,
    desc_cfg=_desc_cfg,
    desc_strs=_desc_strs,
    open_itf_cb=_open_itf_cb,
    reset_cb=_reset_cb,
    control_xfer_cb=_control_xfer_cb,
)
usbd.active(1)
