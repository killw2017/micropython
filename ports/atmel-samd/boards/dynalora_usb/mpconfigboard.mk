USB_VID = 0x04D8
USB_PID = 0xEA2A
USB_PRODUCT = "DynaLoRa_USB"
USB_MANUFACTURER = "BHDynamics"

CHIP_VARIANT = SAMD21E18A
CHIP_FAMILY = samd21

SPI_FLASH_FILESYSTEM = 1
EXTERNAL_FLASH_DEVICES = GD25Q32C
LONGINT_IMPL = MPZ

CIRCUITPY_FULL_BUILD = 0

CIRCUITPY_DISPLAYIO = 0
CIRCUITPY_SDCARDIO = 1

FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_NeoPixel
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_RFM9x