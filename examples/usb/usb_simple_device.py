import machine

VID = 0xF055
PID = 0x9999

EP_OUT = 0x01
EP_IN = 0x81

_descriptor_device = bytes([
    0x12,  # bLength
    0x01,  # bDescriptorType: Device
    0x00, 0x02,  # USB version: 2.00
    0xFF,  # bDeviceClass: vendor
    0x00,  # bDeviceSubClass
    0x01,  # bDeviceProtocol
    0x40,  # bMaxPacketSize
    VID & 0xff, VID >> 8 & 0xff,  # VID
    PID & 0xff, PID >> 8 & 0xff,  # PID
    0x00, 0x01,  # bcdDevice: 1.00
    0x11,  # iManufacturer
    0x12,  # iProduct
    0x13,  # iSerialNumber
    0x01,  # bNumConfigurations: 1
])

_descriptor_config = bytes([
    # Configuration Descriptor.
    0x09,  # bLength
    0x02,  # bDescriptorType: configuration
    0x20, 0x00,  # wTotalLength: 32
    0x01,  # bNumInterfaces
    0x01,  # bConfigurationValue
    0x14,  # iConfiguration
    0xA0,  # bmAttributes
    0x96,  # bMaxPower
    # Interface Descriptor.
    0x09,  # bLength
    0x04,  # bDescriptorType: interface
    0x00,  # bInterfaceNumber
    0x00,  # bAlternateSetting
    0x02,  # bNumEndpoints
    0xFF,  # bInterfaceClass
    0x03,  # bInterfaceSubClass
    0x00,  # bInterfaceProtocol
    0x15,  # iInterface
    # Endpoint IN1.
    0x07,  # bLength
    0x05,  # bDescriptorType: endpoint
    EP_IN,  # bEndpointAddress
    0x03,  # bmAttributes: interrupt
    0x40, 0x00,  # wMaxPacketSize
    0x0A,  # bInterval
    # Endpoint OUT1.
    0x07,  # bLength
    0x05,  # bDescriptorType: endpoint
    EP_OUT,  # bEndpointAddress
    0x02,  # bmAttributes: bulk
    0x40, 0x00,  # wMaxPacketSize
    0x00,  # bInterval
])

class descr_strings:
    def __getitem__(self, value):
        print('get', value)
        if value == 0:
            return None#b'\x04\x03\x09\x04'
        return b'test'

_descriptor_strings_dict = {
    #0x00: b'\x04\x03\x09\x04',
    0x11: b"iManufacturer",
    0x12: b"iProduct",
    0x13: b"iSerial",
    0x14: b"iInterface",
    0x15: b"iInterface",
}

_descriptor_strings_list = [
    None,#b'\x04\x03\x09\x04',
    b'a01',
    b'a02',
    b'a03',
    b'a04',
    b'a05',
    b'a06',
    b'a07',
    b'a010',
    b'a011',
    b'a012',
    b'a013',
    b'a014',
    b'a015',
    b'a016',
    b'a017',
    b'a020',
    b'a021',
    b'a022',
    b'a023',
    b'a024',
    b'a025',
    b'a026',
    b'a027',
]

def _open_itf_cb(interface_desc_view):
    print("_open_itf_cb", bytes(interface_desc_view))
    # prepare to receive first data packet on the OUT endpoint
    usbd.submit_xfer(EP_OUT, buf_out)


def _reset_cb():
    print("_reset_cb")


def _control_xfer_cb(stage, request):
    print("_control_xfer_cb", stage, bytes(request))


def _xfer_cb(ep_addr, result, xferred_bytes):
    print("_xfer_cb", ep_addr, result, xferred_bytes)
    if ep_addr == EP_OUT:
        # Received data.
        print(buf_out)
        # Echo data back.
        usbd.submit_xfer(EP_IN, buf_out[:xferred_bytes])
    elif ep_addr == EP_IN:
        # Host got our data.
        usbd.submit_xfer(EP_OUT, buf_out)


buf_out = bytearray(64)

usbd = machine.USBDevice()
usbd.builtin_driver = usbd.BUILTIN_NONE
usbd.config(
    _descriptor_device,
    _descriptor_config,
    #descr_strings(),
    #_descriptor_strings_dict,
    _descriptor_strings_list,
    open_itf_cb=_open_itf_cb,
    reset_cb=_reset_cb,
    control_xfer_cb=_control_xfer_cb,
    xfer_cb=_xfer_cb,
)
usbd.active(1)
