# cmake file for Pimoroni Tiny 2040

# Freeze board.py
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

# Provide different variants for the downloads page
set(BOARD_VARIANTS "flash_2mb flash_8mb")

# Select the 8MB variant as the default
set(PICO_BOARD "pimoroni_tiny2040")

if("${BOARD_VARIANT}" STREQUAL "flash_2mb")
set(PICO_BOARD "pimoroni_tiny2040_2mb")
endif()
