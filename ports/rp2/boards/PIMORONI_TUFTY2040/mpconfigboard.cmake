# A pico-sdk board definition is required since Tufty 2040 is not yet
# officially supported.
# Add the BOARD dir to the list of paths searched for Pico board headers.
#
# Officially supported boards:
#     https://github.com/raspberrypi/pico-sdk/tree/master/src/boards/include/boards
list(APPEND PICO_BOARD_HEADER_DIRS ${MICROPY_BOARD_DIR})