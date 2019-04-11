#pragma once

#include <stdbool.h>
#include "bluetooth/bluetooth.h"
#include "py/obj.h"
#include "esp_bt_defs.h"

// Enables the Bluetooth stack. Returns errno on failure.
int mp_bt_enable(void);

// Disables the Bluetooth stack. Is a no-op when not enabled.
int mp_bt_disable(void);

// Returns true when the Bluetooth stack is enabled.
bool mp_bt_is_enabled(void);

// Enables the Bluetooth scan. Returns errno on failure.
int mp_bt_scan(void);

// Try to connect to address. Returns errno on failure.
int mp_bt_connect(esp_bd_addr_t device);

// Try to disconnect from remote device. Returns errno on failure.
int mp_bt_disconnect(esp_bd_addr_t device);

// Discover BT characteristics and display them. Returns errno on failure.
int mp_bt_discover_characteristics(void);

// Write to characteristic. Returns errno on failure.
int mp_bt_char_write_handle(uint16_t handle, uint8_t* value, uint8_t length, bool wait_for_response);

// Read char from characteristic and return bytearray. Returns errno on failure.
int mp_bt_char_read(uint16_t value_handle, void *value, size_t *value_len);

// Data types of advertisement packet.
#define MP_BLE_GAP_AD_TYPE_FLAG                  (0x01)
#define MP_BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME   (0x09)

// Flags element of advertisement packet.
#define MP_BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE         (0x02)  // discoverable for everyone
#define MP_BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED         (0x04)  // BLE only - no classic BT supported
#define MP_BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE   (MP_BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE | MP_BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED)

#define MP_BLE_FLAG_READ     (1 << 1)
#define MP_BLE_FLAG_WRITE    (1 << 3)
#define MP_BLE_FLAG_NOTIFY   (1 << 4)
