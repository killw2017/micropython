#if MICROPY_PY_BLUETOOTH

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "py/mperrno.h"
#include "py/runtime.h"
#include "extmod/modupygatt.h"

#define GATTC_TAG "APP"
#define REMOTE_SERVICE_UUID        0xca9e //0xCA9E
#define REMOTE_NOTIFY_CHAR_UUID    0xca9e //0xFF01
#define PROFILE_NUM      1
#define PROFILE_A_APP_ID 0
#define INVALID_HANDLE   0

/*
enum {
  BLE_ADDR_TYPE_PUBLIC = 0x00
  BLE_ADDR_TYPE_RANDOM = 0x01
  BLE_ADDR_TYPE_RPA_PUBLIC = 0x02
  BLE_ADDR_TYPE_RPA_RANDOM = 0x03
}
*/

STATIC bool is_scanning    = false;
STATIC bool get_server = false;
STATIC esp_gattc_char_elem_t *char_elem_result   = NULL;
STATIC esp_gattc_descr_elem_t *descr_elem_result = NULL;

/* Declare static functions */
STATIC void mp_bt_gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
STATIC void mp_bt_gattc_callback(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);
STATIC void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

// Semaphore to serialze asynchronous calls.
STATIC SemaphoreHandle_t mp_bt_call_complete;
STATIC esp_bt_status_t mp_bt_call_status;
STATIC union {
    // Ugly hack to return values from an event handler back to a caller.
    esp_gatt_if_t gattc_if;
    uint16_t      service_handle;
    uint16_t      attr_handle;
} mp_bt_call_result;

struct gattc_profile_inst {
    esp_gattc_cb_t gattc_cb;
    uint16_t gattc_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t char_handle;
    esp_bd_addr_t remote_bda;
};

/* One gatt-based profile one app_id and one gattc_if, this array will store the gattc_if returned by ESP_GATTS_REG_EVT */
static struct gattc_profile_inst gl_profile_tab[PROFILE_NUM] = {
    [PROFILE_A_APP_ID] = {
        .gattc_cb = gattc_profile_event_handler,
        .gattc_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

// Convert an esp_err_t into an errno number.
STATIC int mp_bt_esp_errno(esp_err_t err) {
    if (err != ESP_OK) {
        return MP_EPERM;
    }
    return 0;
}

// Convert the result of an asynchronous call to an errno value.
STATIC int mp_bt_status_errno(void) {
    if (mp_bt_call_status != ESP_BT_STATUS_SUCCESS) {
        return MP_EPERM;
    }
    return 0;
}

STATIC esp_bt_uuid_t remote_filter_service_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_SERVICE_UUID,},
};

STATIC esp_bt_uuid_t remote_filter_char_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = REMOTE_NOTIFY_CHAR_UUID,},
};

STATIC esp_bt_uuid_t notify_descr_uuid = {
    .len = ESP_UUID_LEN_16,
    .uuid = {.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,},
};

// Initialize at early boot.
void mp_bt_init(void) {
    printf("mp_bt_init on core %d\r\n", xPortGetCoreID());
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    mp_bt_call_complete = xSemaphoreCreateBinary();
}

int mp_bt_enable(void) {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_LOGW(GATTC_TAG, "Attempting to call esp_bt_controller_init");
    esp_err_t err = esp_bt_controller_init(&bt_cfg);
    if (err != ESP_OK) {
        return mp_bt_esp_errno(err);
    }
    ESP_LOGW(GATTC_TAG, "Attempting to call esp_bt_controller_enable");
    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK) {
        return mp_bt_esp_errno(err);
    }
    ESP_LOGW(GATTC_TAG, "Attempting to call esp_bluedroid_init");
    err = esp_bluedroid_init();
    if (err != ESP_OK) {
        return mp_bt_esp_errno(err);
    }
    ESP_LOGW(GATTC_TAG, "Attempting to call esp_bluedroid_enable");
    err = esp_bluedroid_enable();
    if (err != ESP_OK) {
        return mp_bt_esp_errno(err);
    }
    ESP_LOGW(GATTC_TAG, "Attempting to call esp_ble_gap_register_callback");
    err = esp_ble_gap_register_callback(mp_bt_gap_callback);
    if (err != ESP_OK) {
        return mp_bt_esp_errno(err);
    }
    ESP_LOGW(GATTC_TAG, "Attempting to call esp_ble_gattc_register_callback");
    err = esp_ble_gattc_register_callback(mp_bt_gattc_callback);
    if (err != ESP_OK) {
        return mp_bt_esp_errno(err);
    }
    ESP_LOGW(GATTC_TAG, "Attempting to call esp_ble_gattc_app_register");
    err = esp_ble_gattc_app_register(PROFILE_A_APP_ID);
    if (err != ESP_OK) {
        return mp_bt_esp_errno(err);
    }
    ESP_LOGW(GATTC_TAG, "Attempting to call esp_ble_gatt_set_local_mtu");
    err = esp_ble_gatt_set_local_mtu(500);
    if (err != ESP_OK) {
        return mp_bt_esp_errno(err);
    }
    return 0;
}

int mp_bt_disable(void) {
  esp_err_t err;
  err = esp_bluedroid_disable();
  if (err != ESP_OK) {
      return mp_bt_esp_errno(err);
  }
  err = esp_bluedroid_deinit();
  if (err != ESP_OK) {
      return mp_bt_esp_errno(err);
  }
  err = esp_bt_controller_disable();
  if (err != ESP_OK) {
      return mp_bt_esp_errno(err);
  }
  err = esp_bt_controller_deinit();
  if (err != ESP_OK) {
      return mp_bt_esp_errno(err);
  }
  return 0;
}

bool mp_bt_is_enabled(void) {
    return esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED;
}

int mp_bt_scan(void) {
  esp_err_t err;
  static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_ENABLE
	};
  is_scanning = true;
  err = esp_ble_gap_set_scan_params(&ble_scan_params);
	if (err != ESP_OK) {
		return mp_bt_esp_errno(err);
	}
  err = esp_ble_gap_start_scanning(10);
	if (err != ESP_OK) {
		return mp_bt_esp_errno(err);
	}
  //Wait for ESP_GAP_BLE_SCAN_RESULT_EVT
  xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);
  printf("Wait for scans...\r\n");

  return 0;
}

int mp_bt_connect(esp_bd_addr_t device) {
  esp_err_t err;

  if (is_scanning) {
    err = esp_ble_gap_stop_scanning();
    if (err != ESP_OK) {
      return mp_bt_esp_errno(err);
    }
    //Wait for ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT
    xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);
  }
  ESP_LOGI(GATTC_TAG, "connect to the remote device.");
  err = esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, device, BLE_ADDR_TYPE_RANDOM, true);
  if (err != ESP_OK) {
		return mp_bt_esp_errno(err);
	}
  //Wait for ESP_GATTC_CFG_MTU_EVT
  xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);

  return 0;
}

int mp_bt_disconnect(esp_bd_addr_t device) {
  esp_err_t err;
  ESP_LOGI(GATTC_TAG, "disconnect from remote device");

  err = esp_ble_gattc_close(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id);

  if (err != ESP_OK) {
    return mp_bt_esp_errno(err);
  }

  err = esp_ble_gap_disconnect(device);

  if (err != ESP_OK) {
    return mp_bt_esp_errno(err);
  }

  return 0;
}

int mp_bt_discover_characteristics(void) {
  esp_err_t err;
  ESP_LOGI(GATTC_TAG, "discovering characteristics of the remote device.");

  err = esp_ble_gattc_search_service(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id, NULL); //&remote_filter_service_uuid);

  if (err != ESP_OK) {
		return mp_bt_esp_errno(err);
	}
  //Wait for ESP_GATTC_SEARCH_CMPL_EVT
  xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);

  return 0;
}

int mp_bt_char_write_handle(uint16_t handle, uint8_t* value, uint8_t length, bool wait_for_response) {
  esp_err_t err;
  ESP_LOGI(GATTC_TAG, "ATTEMTING TO WRITE TO CHARACTERISTIC in HANDLE 0x%04x", handle);
  uint8_t data[length];
  memcpy(data, value, length);
  esp_log_buffer_hex(GATTC_TAG, data, length);
  err = esp_ble_gattc_write_char( gl_profile_tab[PROFILE_A_APP_ID].gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id, handle, length, data, ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
		return mp_bt_esp_errno(err);
	}
  //Wait for ESP_GATTC_WRITE_CHAR_EVT
  xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);
  return 0;
}

int mp_bt_char_read(uint16_t value_handle, void *value, size_t *value_len) {
  esp_err_t err;
  ESP_LOGI(GATTC_TAG, "ATTEMTING TO READ CHARACTERISTIC");

  uint16_t bt_len;
  const uint8_t *bt_ptr;
  //Wait for ESP_GATTC_WRITE_CHAR_EVT
  //err = esp_ble_gatts_get_attr_value(characteristic->value_handle, &bt_len, &bt_ptr);
  err = esp_ble_gattc_read_char(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, gl_profile_tab[PROFILE_A_APP_ID].conn_id, value_handle, ESP_GATT_AUTH_REQ_NONE);
  if (err != ESP_OK) {
      return mp_bt_esp_errno(err);
  }

  //Wait for ESP_GATTC_READ_CHAR_EVT
  xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);
  return 0;
}

STATIC void gattc_profile_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
    esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

    switch (event) {
        case ESP_GATTC_CONNECT_EVT:
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_CONNECT_EVT conn_id %d, if %d", p_data->connect.conn_id, gattc_if);
            gl_profile_tab[PROFILE_A_APP_ID].conn_id = p_data->connect.conn_id;
            gl_profile_tab[PROFILE_A_APP_ID].gattc_if = gattc_if;
            mp_bt_call_result.service_handle = 0xCA9E;
            memcpy(gl_profile_tab[PROFILE_A_APP_ID].remote_bda, p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
            ESP_LOGI(GATTC_TAG, "REMOTE BDA:");
            esp_log_buffer_hex(GATTC_TAG, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, sizeof(esp_bd_addr_t));
            esp_err_t mtu_ret = esp_ble_gattc_send_mtu_req (gattc_if, p_data->connect.conn_id);
            if (mtu_ret){
                ESP_LOGE(GATTC_TAG, "config MTU error, error code = %x", mtu_ret);
            }
            break;

        case ESP_GATTC_OPEN_EVT:
            if (param->open.status != ESP_GATT_OK) {
                ESP_LOGE(GATTC_TAG, "open failed, status %d", p_data->open.status);
                break;
            }
            ESP_LOGI(GATTC_TAG, "open success");
            break;
        case ESP_GATTC_CFG_MTU_EVT:
            if (param->cfg_mtu.status != ESP_GATT_OK) {
                ESP_LOGE(GATTC_TAG,"config mtu failed, error status = %x", param->cfg_mtu.status);
            }
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_CFG_MTU_EVT, Status %d, MTU %d, conn_id %d", param->cfg_mtu.status, param->cfg_mtu.mtu, param->cfg_mtu.conn_id);
            //Return for esp_ble_gattc_open
            xSemaphoreGive(mp_bt_call_complete);
            break;
        case ESP_GATTC_SEARCH_RES_EVT: {
            ESP_LOGI(GATTC_TAG, "SEARCH RES: conn_id = %x is primary service %d", p_data->search_res.conn_id, p_data->search_res.is_primary);
            ESP_LOGI(GATTC_TAG, "start handle %d end handle %d current handle value %d", p_data->search_res.start_handle, p_data->search_res.end_handle, p_data->search_res.srvc_id.inst_id);
            //if (p_data->search_res.srvc_id.uuid.len == ESP_UUID_LEN_16 && p_data->search_res.srvc_id.uuid.uuid.uuid16 == REMOTE_SERVICE_UUID) {
                ESP_LOGI(GATTC_TAG, "service found");
                get_server = true;
                gl_profile_tab[PROFILE_A_APP_ID].service_start_handle = p_data->search_res.start_handle;
                gl_profile_tab[PROFILE_A_APP_ID].service_end_handle = p_data->search_res.end_handle;
                ESP_LOGI(GATTC_TAG, "UUID16: %x", p_data->search_res.srvc_id.uuid.uuid.uuid16);
            //}
            break;
        }
        case ESP_GATTC_SEARCH_CMPL_EVT:
            if (p_data->search_cmpl.status != ESP_GATT_OK) {
                ESP_LOGE(GATTC_TAG, "search service failed, error status = %x", p_data->search_cmpl.status);
                break;
            }
            if(p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_REMOTE_DEVICE) {
                ESP_LOGI(GATTC_TAG, "Get service information from remote device");
            } else if (p_data->search_cmpl.searched_service_source == ESP_GATT_SERVICE_FROM_NVS_FLASH) {
                ESP_LOGI(GATTC_TAG, "Get service information from flash");
            } else {
                ESP_LOGI(GATTC_TAG, "unknown service source");
            }
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_SEARCH_CMPL_EVT");
            if (get_server){
                uint16_t count = 0;
                esp_gatt_status_t status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                         p_data->search_cmpl.conn_id,
                                                                         ESP_GATT_DB_CHARACTERISTIC,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                         INVALID_HANDLE,
                                                                         &count);
                if (status != ESP_GATT_OK) {
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
                }
                 //xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);
                if (count > 0) {
                    // char_elem_result = (esp_gattc_char_elem_t *)malloc(sizeof(esp_gattc_char_elem_t) * count);
                    // if (!char_elem_result){
                    //     ESP_LOGE(GATTC_TAG, "gattc no mem");
                    // } else {
                    //    status = esp_ble_gattc_get_char_by_uuid( gattc_if,
                    //                                              p_data->search_cmpl.conn_id,
                    //                                              gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                    //                                              gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                    //                                              remote_filter_char_uuid,
                    //                                              char_elem_result,
                    //                                              &count);
                    //
                    //     if (status != ESP_GATT_OK) {
                    //         ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_char_by_uuid error");
                    //     }
                    //     xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);
                    //     /*  Every service have only one char in our 'ESP_GATTS_DEMO' demo, so we used first 'char_elem_result' */
                    //     if (count > 0 && (char_elem_result[0].properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY)) {
                    //         gl_profile_tab[PROFILE_A_APP_ID].char_handle = char_elem_result[0].char_handle;
                    //         esp_ble_gattc_register_for_notify (gattc_if, gl_profile_tab[PROFILE_A_APP_ID].remote_bda, char_elem_result[0].char_handle);
                    //         xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);
                    //     }
                    // }
                    // /* free char_elem_result */
                    // free(char_elem_result);
                } else {
                    ESP_LOGE(GATTC_TAG, "no char found");
                }
            }
            //Return for esp_ble_gattc_search_service
            xSemaphoreGive(mp_bt_call_complete);
            break;
        case ESP_GATTC_REG_FOR_NOTIFY_EVT:
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_REG_FOR_NOTIFY_EVT");
            if (p_data->reg_for_notify.status != ESP_GATT_OK) {
                ESP_LOGE(GATTC_TAG, "REG FOR NOTIFY failed: error status = %d", p_data->reg_for_notify.status);
            } else {
                uint16_t count = 0;
                uint16_t notify_en = 1;
                esp_gatt_status_t ret_status = esp_ble_gattc_get_attr_count( gattc_if,
                                                                             gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                             ESP_GATT_DB_DESCRIPTOR,
                                                                             gl_profile_tab[PROFILE_A_APP_ID].service_start_handle,
                                                                             gl_profile_tab[PROFILE_A_APP_ID].service_end_handle,
                                                                             gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                                             &count);

                if (ret_status != ESP_GATT_OK) {
                    ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_attr_count error");
                }
                //xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);

                if (count > 0) {
                    descr_elem_result = malloc(sizeof(esp_gattc_descr_elem_t) * count);
                    if (!descr_elem_result) {
                        ESP_LOGE(GATTC_TAG, "malloc error, gattc no mem");
                    } else {
                        ret_status = esp_ble_gattc_get_descr_by_char_handle( gattc_if,
                                                                             gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                             p_data->reg_for_notify.handle,
                                                                             notify_descr_uuid,
                                                                             descr_elem_result,
                                                                             &count);

                        if (ret_status != ESP_GATT_OK) {
                            ESP_LOGE(GATTC_TAG, "esp_ble_gattc_get_descr_by_char_handle error");
                        }
                        //xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);
                        /* Every char has only one descriptor in our 'ESP_GATTS_DEMO' demo, so we used first 'descr_elem_result' */
                        if (count > 0 && descr_elem_result[0].uuid.len == ESP_UUID_LEN_16 && descr_elem_result[0].uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
                            ret_status = esp_ble_gattc_write_char_descr( gattc_if,
                                                                         gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                                                         descr_elem_result[0].handle,
                                                                         sizeof(notify_en),
                                                                         (uint8_t *)&notify_en,
                                                                         ESP_GATT_WRITE_TYPE_RSP,
                                                                         ESP_GATT_AUTH_REQ_NONE);
                        }

                        if (ret_status != ESP_GATT_OK) {
                            ESP_LOGE(GATTC_TAG, "esp_ble_gattc_write_char_descr error");
                        }
                        //xSemaphoreTake(mp_bt_call_complete, portMAX_DELAY);
                        /* free descr_elem_result */
                        free(descr_elem_result);
                    }
                }
                else {
                    ESP_LOGE(GATTC_TAG, "decsr not found");
                }

            }
            break;

        case ESP_GATTC_NOTIFY_EVT:
            if (p_data->notify.is_notify) {
                ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, receive notify value:");
            } else {
                ESP_LOGI(GATTC_TAG, "ESP_GATTC_NOTIFY_EVT, receive indicate value:");
            }
            esp_log_buffer_hex(GATTC_TAG, p_data->notify.value, p_data->notify.value_len);
            break;
        case ESP_GATTC_WRITE_DESCR_EVT:
            if (p_data->write.status != ESP_GATT_OK) {
                ESP_LOGE(GATTC_TAG, "write descr failed, error status = %x", p_data->write.status);
                break;
            }
            ESP_LOGI(GATTC_TAG, "write descr success ");
            break;
        case ESP_GATTC_SRVC_CHG_EVT: {
            esp_bd_addr_t bda;
            memcpy(bda, p_data->srvc_chg.remote_bda, sizeof(esp_bd_addr_t));
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_SRVC_CHG_EVT, bd_addr:");
            esp_log_buffer_hex(GATTC_TAG, bda, sizeof(esp_bd_addr_t));
            break;
          }
        case ESP_GATTC_WRITE_CHAR_EVT:
            if (p_data->write.status != ESP_GATT_OK) {
                ESP_LOGE(GATTC_TAG, "write char failed, error status = %x", p_data->write.status);
                break;
            }
            ESP_LOGI(GATTC_TAG, "write char success ");
            xSemaphoreGive(mp_bt_call_complete);
            break;
        case ESP_GATTC_READ_CHAR_EVT:
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_READ_CHAR_EVT");
            ESP_LOGI(GATTC_TAG, "p_data->read.value %s",p_data->read.value);
            ESP_LOGI(GATTC_TAG, "p_data->read.value_len %d",p_data->read.value_len);
            esp_log_buffer_hex(GATTC_TAG, p_data->read.value, p_data->read.value_len);
            xSemaphoreGive(mp_bt_call_complete);
            break;
        case ESP_GATTC_DISCONNECT_EVT:
            get_server = false;
            ESP_LOGI(GATTC_TAG, "ESP_GATTC_DISCONNECT_EVT, reason = %d", p_data->disconnect.reason);
            ESP_LOGI(GATTC_TAG, "ESP_GATT_CONN_NONE: %d", p_data->disconnect.reason == ESP_GATT_CONN_NONE);
            break;
        default:
            ESP_LOGI(GATTC_TAG, "GATTC_PROFILE_EVENT_HANDLER: unknown event: %d", event);
            break;

    }
}

STATIC void mp_bt_gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  uint8_t *adv_name = NULL;
  uint8_t adv_name_len = 0;
    switch (event) {
      case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        //scan start complete event to indicate scan start successfully or failed
        if (param->scan_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "scan start failed, error status = %x", param->scan_start_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "scan start success");
        //xSemaphoreGive(mp_bt_call_complete);
        break;
      case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        if (param->scan_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTC_TAG, "scan stop failed, error status = %x", param->scan_stop_cmpl.status);
            break;
        }
        ESP_LOGI(GATTC_TAG, "stop scan successfully");
        //Return for esp_ble_gap_stop_scanning
        xSemaphoreGive(mp_bt_call_complete);
        break;
      case ESP_GAP_BLE_SCAN_RESULT_EVT:
        esp_log_buffer_hex(GATTC_TAG, param->scan_rst.bda, 6);
        ESP_LOGI(GATTC_TAG, "searched Adv Data Len %d, Scan Response Len %d", param->scan_rst.adv_data_len, param->scan_rst.scan_rsp_len);
        adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);
        ESP_LOGI(GATTC_TAG, "searched Device Name Len %d", adv_name_len);
        ESP_LOGI(GATTC_TAG, "searched Device Name Len %d", adv_name_len);
        esp_log_buffer_char(GATTC_TAG, adv_name, adv_name_len);
        #if CONFIG_EXAMPLE_DUMP_ADV_DATA_AND_SCAN_RESP
          if (param->scan_rst.adv_data_len > 0) {
            ESP_LOGI(GATTC_TAG, "adv data:");
            esp_log_buffer_hex(GATTC_TAG, &param->scan_rst.ble_adv[0], param->scan_rst.adv_data_len);
          }
          if (param->scan_rst.scan_rsp_len > 0) {
            ESP_LOGI(GATTC_TAG, "scan resp:");
            esp_log_buffer_hex(GATTC_TAG, &param->scan_rst.ble_adv[param->scan_rst.adv_data_len], param->scan_rst.scan_rsp_len);
          }
        #endif
        ESP_LOGI(GATTC_TAG, "\n");
        static const char remote_device_name[] = "RK-G201S";
        if (adv_name != NULL) {
                if (strlen(remote_device_name) == adv_name_len && strncmp((char *)adv_name, remote_device_name, adv_name_len) == 0) {
                    ESP_LOGI(GATTC_TAG, "searched device %s\n", remote_device_name);
                    ESP_LOGI(GATTC_TAG, "connect to the remote device.");
                    esp_ble_gap_stop_scanning();
                    esp_ble_gattc_open(gl_profile_tab[PROFILE_A_APP_ID].gattc_if, param->scan_rst.bda, param->scan_rst.ble_addr_type, true);
                }
            }
        // Return for esp_ble_gap_start_scanning
        xSemaphoreGive(mp_bt_call_complete);
        break;
      case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        //xSemaphoreGive(mp_bt_call_complete);
        break;
      case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        //xSemaphoreGive(mp_bt_call_complete);
        break;
      case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        mp_bt_call_status = param->adv_start_cmpl.status;
        // May return an error (queue full) when called from
        // mp_bt_gatts_callback, but that's OK.
        //xSemaphoreGive(mp_bt_call_complete);
        break;
      case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        //xSemaphoreGive(mp_bt_call_complete);
        break;
      case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(GATTC_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
            param->update_conn_params.status,
            param->update_conn_params.min_int,
            param->update_conn_params.max_int,
            param->update_conn_params.conn_int,
            param->update_conn_params.latency,
            param->update_conn_params.timeout);
        //xSemaphoreGive(mp_bt_call_complete);
        break;
      default:
          ESP_LOGI(GATTC_TAG, "GAP: unknown event: %d", event);
          break;
    }
}

STATIC void mp_bt_gattc_callback(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
  esp_ble_gattc_cb_param_t *p_data = (esp_ble_gattc_cb_param_t *)param;

  /* If event is register event, store the gattc_if for each profile */
  if (event == ESP_GATTC_REG_EVT) {
      if (param->reg.status == ESP_GATT_OK) {
          gl_profile_tab[param->reg.app_id].gattc_if = gattc_if;
      } else {
          ESP_LOGI(GATTC_TAG, "reg app failed, app_id %04x, status %d",
                  param->reg.app_id,
                  param->reg.status);
          return;
      }
  }

  /* If the gattc_if equal to profile A, call profile A cb handler,
   * so here call each profile's callback */
  do {
      int idx;
      for (idx = 0; idx < PROFILE_NUM; idx++) {
          if (gattc_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                  gattc_if == gl_profile_tab[idx].gattc_if) {
              if (gl_profile_tab[idx].gattc_cb) {
                  gl_profile_tab[idx].gattc_cb(event, gattc_if, param);
              }
          }
      }
  } while (0);
  //xSemaphoreGive(mp_bt_call_complete);
}

#endif //MICROPY_PY_BLUETOOTH
