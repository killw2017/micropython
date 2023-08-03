# CMake file for Pimoroni Pico LiPo RP2040 boards

# Freeze board.py
set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)

# Provide different variants for the downloads page
set(BOARD_VARIANTS "flash_4mb flash_16mb")

# Select the 16MB variant as the default
set(PICO_BOARD "pimoroni_picolipo_16mb")

if("${BOARD_VARIANT}" STREQUAL "flash_4mb")
set(PICO_BOARD "pimoroni_picolipo_4mb")
endif()