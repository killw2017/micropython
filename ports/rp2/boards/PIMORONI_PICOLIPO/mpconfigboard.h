#define MICROPY_HW_BOARD_NAME "Pimoroni Pico LiPo"

#define MICROPY_HW_USB_VID (0x2E8A)
#define MICROPY_HW_USB_PID (0x1002)

#define MICROPY_HW_UART1_TX  (8)
#define MICROPY_HW_UART1_RX  (9)
#define MICROPY_HW_UART1_CTS (10)
#define MICROPY_HW_UART1_RTS (11)

// Allow 1MB for the firmware image itself, allocate the remainder to the filesystem
#define MICROPY_HW_FLASH_STORAGE_BYTES (PICO_FLASH_SIZE_BYTES - (1 * 1024 * 1024))
