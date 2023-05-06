// A pico-sdk board definition is required since the VCC-GND Studio boards are
// not officially supported.
//
// Officially supported boards:
//     https://github.com/raspberrypi/pico-sdk/tree/master/src/boards/include/boards

#ifndef _BOARDS_VCCGNDSTUDIO_8MB_H
#define _BOARDS_VCCGNDSTUDIO_8MB_H

#include "vccgndstudio_common.h"

#define VCCGNDSTUDIO_8MB

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (8 * 1024 * 1024)
#endif

#endif
