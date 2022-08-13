USB_VID = 0x239A
USB_PID = 0x80C6
USB_PRODUCT = "microS2"
USB_MANUFACTURER = "MicroDev"

IDF_TARGET = esp32s2

INTERNAL_FLASH_FILESYSTEM = 1

# The default queue depth of 16 overflows on release builds,
# so increase it to 32.
CFLAGS += -DCFG_TUD_TASK_QUEUE_SZ=32

CIRCUITPY_ESP_FLASH_MODE = qio
CIRCUITPY_ESP_FLASH_FREQ = 80m
CIRCUITPY_ESP_FLASH_SIZE = 16MB