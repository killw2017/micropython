/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Ayke van Laethem
 * Copyright (c) 2019-2020 Jim Mussared
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/binary.h"
#include "py/gc.h"
#include "py/misc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/qstr.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "extmod/modbluetooth.h"
#include <string.h>

#if MICROPY_PY_BLUETOOTH

#if !MICROPY_ENABLE_SCHEDULER
#error modbluetooth requires MICROPY_ENABLE_SCHEDULER
#endif

// NimBLE can have fragmented data for GATTC events, so requires reassembly.
#define MICROPY_PY_BLUETOOTH_USE_GATTC_EVENT_DATA_REASSEMBLY MICROPY_BLUETOOTH_NIMBLE

#define MP_BLUETOOTH_CONNECT_DEFAULT_SCAN_DURATION_MS 2000

#define MICROPY_PY_BLUETOOTH_MAX_EVENT_DATA_TUPLE_LEN 5

// bluetooth.BLE type. This is currently a singleton, however in the future
// this could allow having multiple BLE interfaces on different UARTs.
typedef struct {
    mp_obj_base_t base;
    mp_obj_t irq_handler;
} mp_obj_bluetooth_ble_t;

STATIC const mp_obj_type_t mp_type_bluetooth_ble;

// TODO: this seems like it could be generic?
STATIC mp_obj_t bluetooth_handle_errno(int err) {
    if (err != 0) {
        mp_raise_OSError(err);
    }
    return mp_const_none;
}

// ----------------------------------------------------------------------------
// UUID object
// ----------------------------------------------------------------------------

STATIC mp_obj_t bluetooth_uuid_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    (void)type;

    mp_arg_check_num(n_args, n_kw, 1, 1, false);

    mp_obj_bluetooth_uuid_t *self = mp_obj_malloc(mp_obj_bluetooth_uuid_t, &mp_type_bluetooth_uuid);

    if (mp_obj_is_int(all_args[0])) {
        self->type = MP_BLUETOOTH_UUID_TYPE_16;
        mp_int_t value = mp_obj_get_int(all_args[0]);
        if (value > 65535) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid UUID"));
        }
        self->data[0] = value & 0xff;
        self->data[1] = (value >> 8) & 0xff;
    } else {
        mp_buffer_info_t uuid_bufinfo = {0};
        mp_get_buffer_raise(all_args[0], &uuid_bufinfo, MP_BUFFER_READ);
        if (uuid_bufinfo.len == 2 || uuid_bufinfo.len == 4 || uuid_bufinfo.len == 16) {
            // Bytes data -- infer UUID type from length and copy data.
            self->type = uuid_bufinfo.len;
            memcpy(self->data, uuid_bufinfo.buf, self->type);
        } else {
            // Assume UUID string (e.g. '6E400001-B5A3-F393-E0A9-E50E24DCCA9E')
            self->type = MP_BLUETOOTH_UUID_TYPE_128;
            int uuid_i = 32;
            for (size_t i = 0; i < uuid_bufinfo.len; i++) {
                char c = ((char *)uuid_bufinfo.buf)[i];
                if (c == '-') {
                    continue;
                }
                if (!unichar_isxdigit(c)) {
                    mp_raise_ValueError(MP_ERROR_TEXT("invalid char in UUID"));
                }
                c = unichar_xdigit_value(c);
                uuid_i--;
                if (uuid_i < 0) {
                    mp_raise_ValueError(MP_ERROR_TEXT("UUID too long"));
                }
                if (uuid_i % 2 == 0) {
                    // lower nibble
                    self->data[uuid_i / 2] |= c;
                } else {
                    // upper nibble
                    self->data[uuid_i / 2] = c << 4;
                }
            }
            if (uuid_i > 0) {
                mp_raise_ValueError(MP_ERROR_TEXT("UUID too short"));
            }
        }
    }

    return MP_OBJ_FROM_PTR(self);
}

STATIC mp_obj_t bluetooth_uuid_unary_op(mp_unary_op_t op, mp_obj_t self_in) {
    mp_obj_bluetooth_uuid_t *self = MP_OBJ_TO_PTR(self_in);
    switch (op) {
        case MP_UNARY_OP_HASH: {
            // Use the QSTR hash function.
            return MP_OBJ_NEW_SMALL_INT(qstr_compute_hash(self->data, self->type));
        }
        default:
            return MP_OBJ_NULL;      // op not supported
    }
}

STATIC mp_obj_t bluetooth_uuid_binary_op(mp_binary_op_t op, mp_obj_t lhs_in, mp_obj_t rhs_in) {
    if (!mp_obj_is_type(rhs_in, &mp_type_bluetooth_uuid)) {
        return MP_OBJ_NULL;
    }

    mp_obj_bluetooth_uuid_t *lhs = MP_OBJ_TO_PTR(lhs_in);
    mp_obj_bluetooth_uuid_t *rhs = MP_OBJ_TO_PTR(rhs_in);
    switch (op) {
        case MP_BINARY_OP_EQUAL:
        case MP_BINARY_OP_LESS:
        case MP_BINARY_OP_LESS_EQUAL:
        case MP_BINARY_OP_MORE:
        case MP_BINARY_OP_MORE_EQUAL:
            if (lhs->type == rhs->type) {
                return mp_obj_new_bool(mp_seq_cmp_bytes(op, lhs->data, lhs->type, rhs->data, rhs->type));
            } else {
                return mp_binary_op(op, MP_OBJ_NEW_SMALL_INT(lhs->type), MP_OBJ_NEW_SMALL_INT(rhs->type));
            }

        default:
            return MP_OBJ_NULL; // op not supported
    }
}

STATIC void bluetooth_uuid_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;

    mp_obj_bluetooth_uuid_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "UUID(%s", self->type <= 4 ? "0x" : "'");
    for (int i = 0; i < self->type; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            mp_printf(print, "-");
        }
        mp_printf(print, "%02x", self->data[self->type - 1 - i]);
    }
    if (self->type == MP_BLUETOOTH_UUID_TYPE_128) {
        mp_printf(print, "'");
    }
    mp_printf(print, ")");
}

STATIC mp_int_t bluetooth_uuid_get_buffer(mp_obj_t self_in, mp_buffer_info_t *bufinfo, mp_uint_t flags) {
    mp_obj_bluetooth_uuid_t *self = MP_OBJ_TO_PTR(self_in);

    if (flags != MP_BUFFER_READ) {
        return 1;
    }

    bufinfo->buf = self->data;
    bufinfo->len = self->type;
    bufinfo->typecode = 'B';
    return 0;
}

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_bluetooth_uuid,
    MP_QSTR_UUID,
    MP_TYPE_FLAG_NONE,
    make_new, bluetooth_uuid_make_new,
    unary_op, bluetooth_uuid_unary_op,
    binary_op, bluetooth_uuid_binary_op,
    print, bluetooth_uuid_print,
    buffer, bluetooth_uuid_get_buffer
    );

// ----------------------------------------------------------------------------
// Bluetooth object: General
// ----------------------------------------------------------------------------

STATIC mp_obj_t bluetooth_ble_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    (void)type;
    (void)n_args;
    (void)n_kw;
    (void)all_args;
    if (MP_STATE_VM(bluetooth) == MP_OBJ_NULL) {
        mp_obj_bluetooth_ble_t *o = m_new0(mp_obj_bluetooth_ble_t, 1);
        o->base.type = &mp_type_bluetooth_ble;

        o->irq_handler = mp_const_none;

        MP_STATE_VM(bluetooth) = MP_OBJ_FROM_PTR(o);
    }
    return MP_STATE_VM(bluetooth);
}

STATIC mp_obj_t bluetooth_ble_active(size_t n_args, const mp_obj_t *args) {
    if (n_args == 2) {
        // Boolean enable/disable argument supplied, set current state.
        if (mp_obj_is_true(args[1])) {
            int err = mp_bluetooth_init();
            bluetooth_handle_errno(err);
        } else {
            mp_bluetooth_deinit();
        }
    }
    // Return current state.
    return mp_obj_new_bool(mp_bluetooth_is_active());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_active_obj, 1, 2, bluetooth_ble_active);

STATIC mp_obj_t bluetooth_ble_config(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    if (kwargs->used == 0) {
        // Get config value
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("must query one param"));
        }

        switch (mp_obj_str_get_qstr(args[1])) {
            case MP_QSTR_gap_name: {
                const uint8_t *buf;
                size_t len = mp_bluetooth_gap_get_device_name(&buf);
                return mp_obj_new_bytes(buf, len);
            }
            case MP_QSTR_mac: {
                uint8_t addr_type;
                uint8_t addr[6];
                mp_bluetooth_get_current_address(&addr_type, addr);
                mp_obj_t items[] = { MP_OBJ_NEW_SMALL_INT(addr_type), mp_obj_new_bytes(addr, MP_ARRAY_SIZE(addr)) };
                return mp_obj_new_tuple(2, items);
            }
            case MP_QSTR_mtu:
                return mp_obj_new_int(mp_bluetooth_get_preferred_mtu());
            default:
                mp_raise_ValueError(MP_ERROR_TEXT("unknown config param"));
        }
    } else {
        // Set config value(s)
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("can't specify pos and kw args"));
        }

        for (size_t i = 0; i < kwargs->alloc; ++i) {
            if (MP_MAP_SLOT_IS_FILLED(kwargs, i)) {
                mp_map_elem_t *e = &kwargs->table[i];
                switch (mp_obj_str_get_qstr(e->key)) {
                    case MP_QSTR_gap_name: {
                        mp_buffer_info_t bufinfo;
                        mp_get_buffer_raise(e->value, &bufinfo, MP_BUFFER_READ);
                        bluetooth_handle_errno(mp_bluetooth_gap_set_device_name(bufinfo.buf, bufinfo.len));
                        break;
                    }
                    case MP_QSTR_mtu: {
                        mp_int_t mtu = mp_obj_get_int(e->value);
                        bluetooth_handle_errno(mp_bluetooth_set_preferred_mtu(mtu));
                        break;
                    }
                    case MP_QSTR_addr_mode: {
                        mp_int_t addr_mode = mp_obj_get_int(e->value);
                        mp_bluetooth_set_address_mode(addr_mode);
                        break;
                    }
                    #if MICROPY_PY_BLUETOOTH_ENABLE_PAIRING_BONDING
                    case MP_QSTR_bond: {
                        bool bonding_enabled = mp_obj_is_true(e->value);
                        mp_bluetooth_set_bonding(bonding_enabled);
                        break;
                    }
                    case MP_QSTR_mitm: {
                        bool mitm_protection = mp_obj_is_true(e->value);
                        mp_bluetooth_set_mitm_protection(mitm_protection);
                        break;
                    }
                    case MP_QSTR_io: {
                        mp_int_t io_capability = mp_obj_get_int(e->value);
                        mp_bluetooth_set_io_capability(io_capability);
                        break;
                    }
                    case MP_QSTR_le_secure: {
                        bool le_secure_required = mp_obj_is_true(e->value);
                        mp_bluetooth_set_le_secure(le_secure_required);
                        break;
                    }
                    #endif // MICROPY_PY_BLUETOOTH_ENABLE_PAIRING_BONDING
                    default:
                        mp_raise_ValueError(MP_ERROR_TEXT("unknown config param"));
                }
            }
        }

        return mp_const_none;
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(bluetooth_ble_config_obj, 1, bluetooth_ble_config);

STATIC mp_obj_t bluetooth_ble_irq(mp_obj_t self_in, mp_obj_t handler_in) {
    (void)self_in;
    if (handler_in != mp_const_none && !mp_obj_is_callable(handler_in)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid handler"));
    }

    // Update the callback.
    mp_obj_bluetooth_ble_t *o = MP_OBJ_TO_PTR(MP_STATE_VM(bluetooth));
    o->irq_handler = handler_in;

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(bluetooth_ble_irq_obj, bluetooth_ble_irq);

// ----------------------------------------------------------------------------
// Bluetooth object: GAP
// ----------------------------------------------------------------------------

STATIC mp_obj_t bluetooth_ble_gap_advertise(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_interval_us, ARG_adv_data, ARG_resp_data, ARG_connectable };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_interval_us, MP_ARG_OBJ, {.u_obj = MP_OBJ_NEW_SMALL_INT(500000)} },
        { MP_QSTR_adv_data, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_resp_data, MP_ARG_OBJ | MP_ARG_KW_ONLY, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_connectable, MP_ARG_OBJ | MP_ARG_KW_ONLY, {.u_rom_obj = MP_ROM_TRUE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args - 1, pos_args + 1, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (args[ARG_interval_us].u_obj == mp_const_none) {
        mp_bluetooth_gap_advertise_stop();
        return mp_const_none;
    }

    mp_int_t interval_us = mp_obj_get_int(args[ARG_interval_us].u_obj);
    bool connectable = mp_obj_is_true(args[ARG_connectable].u_obj);

    mp_buffer_info_t adv_bufinfo = {0};
    if (args[ARG_adv_data].u_obj != mp_const_none) {
        mp_get_buffer_raise(args[ARG_adv_data].u_obj, &adv_bufinfo, MP_BUFFER_READ);
    }

    mp_buffer_info_t resp_bufinfo = {0};
    if (args[ARG_resp_data].u_obj != mp_const_none) {
        mp_get_buffer_raise(args[ARG_resp_data].u_obj, &resp_bufinfo, MP_BUFFER_READ);
    }

    return bluetooth_handle_errno(mp_bluetooth_gap_advertise_start(connectable, interval_us, adv_bufinfo.buf, adv_bufinfo.len, resp_bufinfo.buf, resp_bufinfo.len));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(bluetooth_ble_gap_advertise_obj, 1, bluetooth_ble_gap_advertise);

STATIC int bluetooth_gatts_register_service(mp_obj_t uuid_in, mp_obj_t characteristics_in, uint16_t **handles, size_t *num_handles) {
    if (!mp_obj_is_type(uuid_in, &mp_type_bluetooth_uuid)) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid service UUID"));
    }
    mp_obj_bluetooth_uuid_t *service_uuid = MP_OBJ_TO_PTR(uuid_in);

    mp_obj_t len_in = mp_obj_len(characteristics_in);
    size_t len = mp_obj_get_int(len_in);
    mp_obj_iter_buf_t iter_buf;
    mp_obj_t iterable = mp_getiter(characteristics_in, &iter_buf);
    mp_obj_t characteristic_obj;

    // Lists of characteristic uuids and flags.
    mp_obj_bluetooth_uuid_t **characteristic_uuids = m_new(mp_obj_bluetooth_uuid_t *, len);
    uint16_t *characteristic_flags = m_new(uint16_t, len);

    // Flattened list of descriptor uuids and flags. Grows (realloc) as more descriptors are encountered.
    mp_obj_bluetooth_uuid_t **descriptor_uuids = NULL;
    uint16_t *descriptor_flags = NULL;
    // How many descriptors in the flattened list per characteristic.
    uint8_t *num_descriptors = m_new(uint8_t, len);

    // Inititally allocate enough room for the number of characteristics.
    // Will be grown to accommodate descriptors as necessary.
    *num_handles = len;
    *handles = m_new(uint16_t, *num_handles);

    // Extract out characteristic uuids & flags.

    int characteristic_index = 0; // characteristic index.
    int handle_index = 0; // handle index.
    int descriptor_index = 0; // descriptor index.
    while ((characteristic_obj = mp_iternext(iterable)) != MP_OBJ_STOP_ITERATION) {
        // (uuid, flags, (optional descriptors),)
        size_t characteristic_len;
        mp_obj_t *characteristic_items;
        mp_obj_get_array(characteristic_obj, &characteristic_len, &characteristic_items);

        if (characteristic_len < 2 || characteristic_len > 3) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid characteristic tuple"));
        }
        mp_obj_t uuid_obj = characteristic_items[0];
        if (!mp_obj_is_type(uuid_obj, &mp_type_bluetooth_uuid)) {
            mp_raise_ValueError(MP_ERROR_TEXT("invalid characteristic UUID"));
        }

        (*handles)[handle_index++] = 0xffff;

        // Optional third element, iterable of descriptors.
        if (characteristic_len >= 3) {
            mp_int_t n = mp_obj_get_int(mp_obj_len(characteristic_items[2]));
            if (n) {
                num_descriptors[characteristic_index] = n;

                // Grow the flattened uuids and flags arrays with this many more descriptors.
                descriptor_uuids = m_renew(mp_obj_bluetooth_uuid_t *, descriptor_uuids, descriptor_index, descriptor_index + num_descriptors[characteristic_index]);
                descriptor_flags = m_renew(uint16_t, descriptor_flags, descriptor_index, descriptor_index + num_descriptors[characteristic_index]);

                // Also grow the handles array.
                *handles = m_renew(uint16_t, *handles, *num_handles, *num_handles + num_descriptors[characteristic_index]);

                mp_obj_iter_buf_t iter_buf_desc;
                mp_obj_t iterable_desc = mp_getiter(characteristic_items[2], &iter_buf_desc);
                mp_obj_t descriptor_obj;

                // Extract out descriptors for this characteristic.
                while ((descriptor_obj = mp_iternext(iterable_desc)) != MP_OBJ_STOP_ITERATION) {
                    // (uuid, flags,)
                    mp_obj_t *descriptor_items;
                    mp_obj_get_array_fixed_n(descriptor_obj, 2, &descriptor_items);
                    mp_obj_t desc_uuid_obj = descriptor_items[0];
                    if (!mp_obj_is_type(desc_uuid_obj, &mp_type_bluetooth_uuid)) {
                        mp_raise_ValueError(MP_ERROR_TEXT("invalid descriptor UUID"));
                    }

                    descriptor_uuids[descriptor_index] = MP_OBJ_TO_PTR(desc_uuid_obj);
                    descriptor_flags[descriptor_index] = mp_obj_get_int(descriptor_items[1]);
                    ++descriptor_index;

                    (*handles)[handle_index++] = 0xffff;
                }

                // Reflect that we've grown the handles array.
                *num_handles += num_descriptors[characteristic_index];
            }
        }

        characteristic_uuids[characteristic_index] = MP_OBJ_TO_PTR(uuid_obj);
        characteristic_flags[characteristic_index] = mp_obj_get_int(characteristic_items[1]);
        ++characteristic_index;
    }

    // Add service.
    return mp_bluetooth_gatts_register_service(service_uuid, characteristic_uuids, characteristic_flags, descriptor_uuids, descriptor_flags, num_descriptors, *handles, len);
}

STATIC mp_obj_t bluetooth_ble_gatts_register_services(mp_obj_t self_in, mp_obj_t services_in) {
    (void)self_in;
    mp_obj_t len_in = mp_obj_len(services_in);
    size_t len = mp_obj_get_int(len_in);
    mp_obj_iter_buf_t iter_buf;
    mp_obj_t iterable = mp_getiter(services_in, &iter_buf);
    mp_obj_t service_tuple_obj;

    mp_obj_tuple_t *result = MP_OBJ_TO_PTR(mp_obj_new_tuple(len, NULL));

    uint16_t **handles = m_new0(uint16_t *, len);
    size_t *num_handles = m_new0(size_t, len);

    // TODO: Add a `append` kwarg (defaulting to False) to make this behavior optional.
    bool append = false;
    int err = mp_bluetooth_gatts_register_service_begin(append);
    if (err != 0) {
        return bluetooth_handle_errno(err);
    }

    size_t i = 0;
    while ((service_tuple_obj = mp_iternext(iterable)) != MP_OBJ_STOP_ITERATION) {
        // (uuid, chars)
        mp_obj_t *service_items;
        mp_obj_get_array_fixed_n(service_tuple_obj, 2, &service_items);
        err = bluetooth_gatts_register_service(service_items[0], service_items[1], &handles[i], &num_handles[i]);
        if (err != 0) {
            return bluetooth_handle_errno(err);
        }

        ++i;
    }

    // On Nimble, this will actually perform the registration, making the handles valid.
    err = mp_bluetooth_gatts_register_service_end();
    if (err != 0) {
        return bluetooth_handle_errno(err);
    }

    // Return tuple of tuple of value handles.
    // TODO: Also the Generic Access service characteristics?
    for (i = 0; i < len; ++i) {
        mp_obj_tuple_t *service_handles = MP_OBJ_TO_PTR(mp_obj_new_tuple(num_handles[i], NULL));
        for (size_t j = 0; j < num_handles[i]; ++j) {
            service_handles->items[j] = MP_OBJ_NEW_SMALL_INT(handles[i][j]);
        }
        result->items[i] = MP_OBJ_FROM_PTR(service_handles);
    }

    // Free temporary arrays.
    m_del(uint16_t *, handles, len);
    m_del(size_t, num_handles, len);

    return MP_OBJ_FROM_PTR(result);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(bluetooth_ble_gatts_register_services_obj, bluetooth_ble_gatts_register_services);

#if MICROPY_PY_BLUETOOTH_ENABLE_CENTRAL_MODE
STATIC mp_obj_t bluetooth_ble_gap_connect(size_t n_args, const mp_obj_t *args) {
    if (n_args == 2) {
        if (args[1] == mp_const_none) {
            int err = mp_bluetooth_gap_peripheral_connect_cancel();
            return bluetooth_handle_errno(err);
        }
        mp_raise_TypeError(MP_ERROR_TEXT("invalid addr"));
    }
    uint8_t addr_type = mp_obj_get_int(args[1]);
    mp_buffer_info_t bufinfo = {0};
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len != 6) {
        mp_raise_ValueError(MP_ERROR_TEXT("invalid addr"));
    }
    mp_int_t scan_duration_ms = MP_BLUETOOTH_CONNECT_DEFAULT_SCAN_DURATION_MS;
    mp_int_t min_conn_interval_us = 0;
    mp_int_t max_conn_interval_us = 0;
    if (n_args >= 4 && args[3] != mp_const_none) {
        scan_duration_ms = mp_obj_get_int(args[3]);
    }
    if (n_args >= 5 && args[4] != mp_const_none) {
        min_conn_interval_us = mp_obj_get_int(args[4]);
    }
    if (n_args >= 6 && args[5] != mp_const_none) {
        max_conn_interval_us = mp_obj_get_int(args[5]);
    }

    int err = mp_bluetooth_gap_peripheral_connect(addr_type, bufinfo.buf, scan_duration_ms, min_conn_interval_us, max_conn_interval_us);
    return bluetooth_handle_errno(err);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gap_connect_obj, 2, 6, bluetooth_ble_gap_connect);

STATIC mp_obj_t bluetooth_ble_gap_scan(size_t n_args, const mp_obj_t *args) {
    // Default is indefinite scan, with the NimBLE "background scan" interval and window.
    mp_int_t duration_ms = 0;
    mp_int_t interval_us = 1280000;
    mp_int_t window_us = 11250;
    bool active_scan = false;
    if (n_args > 1) {
        if (args[1] == mp_const_none) {
            // scan(None) --> stop scan.
            return bluetooth_handle_errno(mp_bluetooth_gap_scan_stop());
        }
        duration_ms = mp_obj_get_int(args[1]);
        if (n_args > 2) {
            interval_us = mp_obj_get_int(args[2]);
            if (n_args > 3) {
                window_us = mp_obj_get_int(args[3]);
                if (n_args > 4) {
                    active_scan = mp_obj_is_true(args[4]);
                }
            }
        }
    }
    return bluetooth_handle_errno(mp_bluetooth_gap_scan_start(duration_ms, interval_us, window_us, active_scan));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gap_scan_obj, 1, 5, bluetooth_ble_gap_scan);
#endif // MICROPY_PY_BLUETOOTH_ENABLE_CENTRAL_MODE

STATIC mp_obj_t bluetooth_ble_gap_disconnect(mp_obj_t self_in, mp_obj_t conn_handle_in) {
    (void)self_in;
    uint16_t conn_handle = mp_obj_get_int(conn_handle_in);
    int err = mp_bluetooth_gap_disconnect(conn_handle);
    if (err == 0) {
        return mp_const_true;
    } else if (err == MP_ENOTCONN) {
        return mp_const_false;
    } else {
        return bluetooth_handle_errno(err);
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(bluetooth_ble_gap_disconnect_obj, bluetooth_ble_gap_disconnect);

#if MICROPY_PY_BLUETOOTH_ENABLE_PAIRING_BONDING
STATIC mp_obj_t bluetooth_ble_gap_pair(mp_obj_t self_in, mp_obj_t conn_handle_in) {
    (void)self_in;
    uint16_t conn_handle = mp_obj_get_int(conn_handle_in);
    return bluetooth_handle_errno(mp_bluetooth_gap_pair(conn_handle));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(bluetooth_ble_gap_pair_obj, bluetooth_ble_gap_pair);

STATIC mp_obj_t bluetooth_ble_gap_passkey(size_t n_args, const mp_obj_t *args) {
    uint16_t conn_handle = mp_obj_get_int(args[1]);
    uint8_t action = mp_obj_get_int(args[2]);
    mp_int_t passkey = mp_obj_get_int(args[3]);
    return bluetooth_handle_errno(mp_bluetooth_gap_passkey(conn_handle, action, passkey));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gap_passkey_obj, 4, 4, bluetooth_ble_gap_passkey);
#endif // MICROPY_PY_BLUETOOTH_ENABLE_PAIRING_BONDING

// ----------------------------------------------------------------------------
// Bluetooth object: GATTS (Peripheral/Advertiser role)
// ----------------------------------------------------------------------------

STATIC mp_obj_t bluetooth_ble_gatts_read(mp_obj_t self_in, mp_obj_t value_handle_in) {
    (void)self_in;
    size_t len = 0;
    const uint8_t *buf;
    mp_bluetooth_gatts_read(mp_obj_get_int(value_handle_in), &buf, &len);
    return mp_obj_new_bytes(buf, len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(bluetooth_ble_gatts_read_obj, bluetooth_ble_gatts_read);

STATIC mp_obj_t bluetooth_ble_gatts_write(size_t n_args, const mp_obj_t *args) {
    mp_buffer_info_t bufinfo = {0};
    mp_get_buffer_raise(args[2], &bufinfo, MP_BUFFER_READ);
    bool send_update = false;
    if (n_args > 3) {
        send_update = mp_obj_is_true(args[3]);
    }
    bluetooth_handle_errno(mp_bluetooth_gatts_write(mp_obj_get_int(args[1]), bufinfo.buf, bufinfo.len, send_update));
    return MP_OBJ_NEW_SMALL_INT(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gatts_write_obj, 3, 4, bluetooth_ble_gatts_write);

STATIC mp_obj_t bluetooth_ble_gatts_notify_indicate(size_t n_args, const mp_obj_t *args, int gatts_op) {
    mp_int_t conn_handle = mp_obj_get_int(args[1]);
    mp_int_t value_handle = mp_obj_get_int(args[2]);

    const uint8_t *value = NULL;
    size_t value_len = 0;
    if (n_args == 4 && args[3] != mp_const_none) {
        mp_buffer_info_t bufinfo = {0};
        mp_get_buffer_raise(args[3], &bufinfo, MP_BUFFER_READ);
        value = bufinfo.buf;
        value_len = bufinfo.len;
    }
    return bluetooth_handle_errno(mp_bluetooth_gatts_notify_indicate(conn_handle, value_handle, gatts_op, value, value_len));
}

STATIC mp_obj_t bluetooth_ble_gatts_notify(size_t n_args, const mp_obj_t *args) {
    return bluetooth_ble_gatts_notify_indicate(n_args, args, MP_BLUETOOTH_GATTS_OP_NOTIFY);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gatts_notify_obj, 3, 4, bluetooth_ble_gatts_notify);

STATIC mp_obj_t bluetooth_ble_gatts_indicate(size_t n_args, const mp_obj_t *args) {
    return bluetooth_ble_gatts_notify_indicate(n_args, args, MP_BLUETOOTH_GATTS_OP_INDICATE);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gatts_indicate_obj, 3, 4, bluetooth_ble_gatts_indicate);

STATIC mp_obj_t bluetooth_ble_gatts_set_buffer(size_t n_args, const mp_obj_t *args) {
    mp_int_t value_handle = mp_obj_get_int(args[1]);
    mp_int_t len = mp_obj_get_int(args[2]);
    bool append = n_args >= 4 && mp_obj_is_true(args[3]);
    return bluetooth_handle_errno(mp_bluetooth_gatts_set_buffer(value_handle, len, append));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gatts_set_buffer_obj, 3, 4, bluetooth_ble_gatts_set_buffer);

// ----------------------------------------------------------------------------
// Bluetooth object: GATTC (Central/Scanner role)
// ----------------------------------------------------------------------------

#if MICROPY_PY_BLUETOOTH_ENABLE_GATT_CLIENT

STATIC mp_obj_t bluetooth_ble_gattc_discover_services(size_t n_args, const mp_obj_t *args) {
    mp_int_t conn_handle = mp_obj_get_int(args[1]);
    mp_obj_bluetooth_uuid_t *uuid = NULL;
    if (n_args == 3 && args[2] != mp_const_none) {
        if (!mp_obj_is_type(args[2], &mp_type_bluetooth_uuid)) {
            mp_raise_TypeError(MP_ERROR_TEXT("UUID"));
        }
        uuid = MP_OBJ_TO_PTR(args[2]);
    }
    return bluetooth_handle_errno(mp_bluetooth_gattc_discover_primary_services(conn_handle, uuid));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gattc_discover_services_obj, 2, 3, bluetooth_ble_gattc_discover_services);

STATIC mp_obj_t bluetooth_ble_gattc_discover_characteristics(size_t n_args, const mp_obj_t *args) {
    mp_int_t conn_handle = mp_obj_get_int(args[1]);
    mp_int_t start_handle = mp_obj_get_int(args[2]);
    mp_int_t end_handle = mp_obj_get_int(args[3]);
    mp_obj_bluetooth_uuid_t *uuid = NULL;
    if (n_args == 5 && args[4] != mp_const_none) {
        if (!mp_obj_is_type(args[4], &mp_type_bluetooth_uuid)) {
            mp_raise_TypeError(MP_ERROR_TEXT("UUID"));
        }
        uuid = MP_OBJ_TO_PTR(args[4]);
    }
    return bluetooth_handle_errno(mp_bluetooth_gattc_discover_characteristics(conn_handle, start_handle, end_handle, uuid));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gattc_discover_characteristics_obj, 4, 5, bluetooth_ble_gattc_discover_characteristics);

STATIC mp_obj_t bluetooth_ble_gattc_discover_descriptors(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    mp_int_t conn_handle = mp_obj_get_int(args[1]);
    mp_int_t start_handle = mp_obj_get_int(args[2]);
    mp_int_t end_handle = mp_obj_get_int(args[3]);
    return bluetooth_handle_errno(mp_bluetooth_gattc_discover_descriptors(conn_handle, start_handle, end_handle));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gattc_discover_descriptors_obj, 4, 4, bluetooth_ble_gattc_discover_descriptors);

STATIC mp_obj_t bluetooth_ble_gattc_read(mp_obj_t self_in, mp_obj_t conn_handle_in, mp_obj_t value_handle_in) {
    (void)self_in;
    mp_int_t conn_handle = mp_obj_get_int(conn_handle_in);
    mp_int_t value_handle = mp_obj_get_int(value_handle_in);
    return bluetooth_handle_errno(mp_bluetooth_gattc_read(conn_handle, value_handle));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(bluetooth_ble_gattc_read_obj, bluetooth_ble_gattc_read);

STATIC mp_obj_t bluetooth_ble_gattc_write(size_t n_args, const mp_obj_t *args) {
    mp_int_t conn_handle = mp_obj_get_int(args[1]);
    mp_int_t value_handle = mp_obj_get_int(args[2]);
    mp_obj_t data = args[3];
    mp_buffer_info_t bufinfo = {0};
    mp_get_buffer_raise(data, &bufinfo, MP_BUFFER_READ);
    unsigned int mode = MP_BLUETOOTH_WRITE_MODE_NO_RESPONSE;
    if (n_args == 5) {
        mode = mp_obj_get_int(args[4]);
    }
    return bluetooth_handle_errno(mp_bluetooth_gattc_write(conn_handle, value_handle, bufinfo.buf, bufinfo.len, mode));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_gattc_write_obj, 4, 5, bluetooth_ble_gattc_write);

STATIC mp_obj_t bluetooth_ble_gattc_exchange_mtu(mp_obj_t self_in, mp_obj_t conn_handle_in) {
    (void)self_in;
    uint16_t conn_handle = mp_obj_get_int(conn_handle_in);
    return bluetooth_handle_errno(mp_bluetooth_gattc_exchange_mtu(conn_handle));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(bluetooth_ble_gattc_exchange_mtu_obj, bluetooth_ble_gattc_exchange_mtu);

#endif // MICROPY_PY_BLUETOOTH_ENABLE_GATT_CLIENT

#if MICROPY_PY_BLUETOOTH_ENABLE_L2CAP_CHANNELS

STATIC mp_obj_t bluetooth_ble_l2cap_listen(mp_obj_t self_in, mp_obj_t psm_in, mp_obj_t mtu_in) {
    (void)self_in;
    mp_int_t psm = mp_obj_get_int(psm_in);
    mp_int_t mtu = MAX(32, MIN(UINT16_MAX, mp_obj_get_int(mtu_in)));
    return bluetooth_handle_errno(mp_bluetooth_l2cap_listen(psm, mtu));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(bluetooth_ble_l2cap_listen_obj, bluetooth_ble_l2cap_listen);

STATIC mp_obj_t bluetooth_ble_l2cap_connect(size_t n_args, const mp_obj_t *args) {
    mp_int_t conn_handle = mp_obj_get_int(args[1]);
    mp_int_t psm = mp_obj_get_int(args[2]);
    mp_int_t mtu = MAX(32, MIN(UINT16_MAX, mp_obj_get_int(args[3])));
    return bluetooth_handle_errno(mp_bluetooth_l2cap_connect(conn_handle, psm, mtu));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_l2cap_connect_obj, 4, 4, bluetooth_ble_l2cap_connect);

STATIC mp_obj_t bluetooth_ble_l2cap_disconnect(mp_obj_t self_in, mp_obj_t conn_handle_in, mp_obj_t cid_in) {
    (void)self_in;
    mp_int_t conn_handle = mp_obj_get_int(conn_handle_in);
    mp_int_t cid = mp_obj_get_int(cid_in);
    return bluetooth_handle_errno(mp_bluetooth_l2cap_disconnect(conn_handle, cid));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(bluetooth_ble_l2cap_disconnect_obj, bluetooth_ble_l2cap_disconnect);

STATIC mp_obj_t bluetooth_ble_l2cap_send(size_t n_args, const mp_obj_t *args) {
    mp_int_t conn_handle = mp_obj_get_int(args[1]);
    mp_int_t cid = mp_obj_get_int(args[2]);
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[3], &bufinfo, MP_BUFFER_READ);
    bool stalled = false;
    bluetooth_handle_errno(mp_bluetooth_l2cap_send(conn_handle, cid, bufinfo.buf, bufinfo.len, &stalled));
    // Return True if the channel is still ready to send.
    return mp_obj_new_bool(!stalled);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_l2cap_send_obj, 4, 4, bluetooth_ble_l2cap_send);

STATIC mp_obj_t bluetooth_ble_l2cap_recvinto(size_t n_args, const mp_obj_t *args) {
    mp_int_t conn_handle = mp_obj_get_int(args[1]);
    mp_int_t cid = mp_obj_get_int(args[2]);
    mp_buffer_info_t bufinfo = {0};
    if (args[3] != mp_const_none) {
        mp_get_buffer_raise(args[3], &bufinfo, MP_BUFFER_WRITE);
    }
    bluetooth_handle_errno(mp_bluetooth_l2cap_recvinto(conn_handle, cid, bufinfo.buf, &bufinfo.len));
    return MP_OBJ_NEW_SMALL_INT(bufinfo.len);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_l2cap_recvinto_obj, 4, 4, bluetooth_ble_l2cap_recvinto);

#endif // MICROPY_PY_BLUETOOTH_ENABLE_L2CAP_CHANNELS

#if MICROPY_PY_BLUETOOTH_ENABLE_HCI_CMD

STATIC mp_obj_t bluetooth_ble_hci_cmd(size_t n_args, const mp_obj_t *args) {
    mp_int_t ogf = mp_obj_get_int(args[1]);
    mp_int_t ocf = mp_obj_get_int(args[2]);
    mp_buffer_info_t bufinfo_request = {0};
    mp_buffer_info_t bufinfo_response = {0};
    mp_get_buffer_raise(args[3], &bufinfo_request, MP_BUFFER_READ);
    mp_get_buffer_raise(args[4], &bufinfo_response, MP_BUFFER_WRITE);
    uint8_t status = 0;
    bluetooth_handle_errno(mp_bluetooth_hci_cmd(ogf, ocf, bufinfo_request.buf, bufinfo_request.len, bufinfo_response.buf, bufinfo_response.len, &status));
    return MP_OBJ_NEW_SMALL_INT(status);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bluetooth_ble_hci_cmd_obj, 5, 5, bluetooth_ble_hci_cmd);

#endif // MICROPY_PY_BLUETOOTH_ENABLE_HCI_CMD

// ----------------------------------------------------------------------------
// Bluetooth object: Definition
// ----------------------------------------------------------------------------

STATIC const mp_rom_map_elem_t bluetooth_ble_locals_dict_table[] = {
    // General
    { MP_ROM_QSTR(MP_QSTR_active), MP_ROM_PTR(&bluetooth_ble_active_obj) },
    { MP_ROM_QSTR(MP_QSTR_config), MP_ROM_PTR(&bluetooth_ble_config_obj) },
    { MP_ROM_QSTR(MP_QSTR_irq), MP_ROM_PTR(&bluetooth_ble_irq_obj) },
    // GAP
    { MP_ROM_QSTR(MP_QSTR_gap_advertise), MP_ROM_PTR(&bluetooth_ble_gap_advertise_obj) },
    #if MICROPY_PY_BLUETOOTH_ENABLE_CENTRAL_MODE
    { MP_ROM_QSTR(MP_QSTR_gap_connect), MP_ROM_PTR(&bluetooth_ble_gap_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_gap_scan), MP_ROM_PTR(&bluetooth_ble_gap_scan_obj) },
    #endif
    { MP_ROM_QSTR(MP_QSTR_gap_disconnect), MP_ROM_PTR(&bluetooth_ble_gap_disconnect_obj) },
    #if MICROPY_PY_BLUETOOTH_ENABLE_PAIRING_BONDING
    { MP_ROM_QSTR(MP_QSTR_gap_pair), MP_ROM_PTR(&bluetooth_ble_gap_pair_obj) },
    { MP_ROM_QSTR(MP_QSTR_gap_passkey), MP_ROM_PTR(&bluetooth_ble_gap_passkey_obj) },
    #endif
    // GATT Server
    { MP_ROM_QSTR(MP_QSTR_gatts_register_services), MP_ROM_PTR(&bluetooth_ble_gatts_register_services_obj) },
    { MP_ROM_QSTR(MP_QSTR_gatts_read), MP_ROM_PTR(&bluetooth_ble_gatts_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_gatts_write), MP_ROM_PTR(&bluetooth_ble_gatts_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_gatts_notify), MP_ROM_PTR(&bluetooth_ble_gatts_notify_obj) },
    { MP_ROM_QSTR(MP_QSTR_gatts_indicate), MP_ROM_PTR(&bluetooth_ble_gatts_indicate_obj) },
    { MP_ROM_QSTR(MP_QSTR_gatts_set_buffer), MP_ROM_PTR(&bluetooth_ble_gatts_set_buffer_obj) },
    #if MICROPY_PY_BLUETOOTH_ENABLE_GATT_CLIENT
    // GATT Client
    { MP_ROM_QSTR(MP_QSTR_gattc_discover_services), MP_ROM_PTR(&bluetooth_ble_gattc_discover_services_obj) },
    { MP_ROM_QSTR(MP_QSTR_gattc_discover_characteristics), MP_ROM_PTR(&bluetooth_ble_gattc_discover_characteristics_obj) },
    { MP_ROM_QSTR(MP_QSTR_gattc_discover_descriptors), MP_ROM_PTR(&bluetooth_ble_gattc_discover_descriptors_obj) },
    { MP_ROM_QSTR(MP_QSTR_gattc_read), MP_ROM_PTR(&bluetooth_ble_gattc_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_gattc_write), MP_ROM_PTR(&bluetooth_ble_gattc_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_gattc_exchange_mtu), MP_ROM_PTR(&bluetooth_ble_gattc_exchange_mtu_obj) },
    #endif
    #if MICROPY_PY_BLUETOOTH_ENABLE_L2CAP_CHANNELS
    { MP_ROM_QSTR(MP_QSTR_l2cap_listen), MP_ROM_PTR(&bluetooth_ble_l2cap_listen_obj) },
    { MP_ROM_QSTR(MP_QSTR_l2cap_connect), MP_ROM_PTR(&bluetooth_ble_l2cap_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_l2cap_disconnect), MP_ROM_PTR(&bluetooth_ble_l2cap_disconnect_obj) },
    { MP_ROM_QSTR(MP_QSTR_l2cap_send), MP_ROM_PTR(&bluetooth_ble_l2cap_send_obj) },
    { MP_ROM_QSTR(MP_QSTR_l2cap_recvinto), MP_ROM_PTR(&bluetooth_ble_l2cap_recvinto_obj) },
    #endif
    #if MICROPY_PY_BLUETOOTH_ENABLE_HCI_CMD
    { MP_ROM_QSTR(MP_QSTR_hci_cmd), MP_ROM_PTR(&bluetooth_ble_hci_cmd_obj) },
    #endif
};
STATIC MP_DEFINE_CONST_DICT(bluetooth_ble_locals_dict, bluetooth_ble_locals_dict_table);

STATIC MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_bluetooth_ble,
    MP_QSTR_BLE,
    MP_TYPE_FLAG_NONE,
    make_new, bluetooth_ble_make_new,
    locals_dict, &bluetooth_ble_locals_dict
    );

STATIC const mp_rom_map_elem_t mp_module_bluetooth_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_bluetooth) },
    { MP_ROM_QSTR(MP_QSTR_BLE), MP_ROM_PTR(&mp_type_bluetooth_ble) },
    { MP_ROM_QSTR(MP_QSTR_UUID), MP_ROM_PTR(&mp_type_bluetooth_uuid) },

    // TODO: Deprecate these flags (recommend copying the constants from modbluetooth.h instead).
    { MP_ROM_QSTR(MP_QSTR_FLAG_READ), MP_ROM_INT(MP_BLUETOOTH_CHARACTERISTIC_FLAG_READ) },
    { MP_ROM_QSTR(MP_QSTR_FLAG_WRITE), MP_ROM_INT(MP_BLUETOOTH_CHARACTERISTIC_FLAG_WRITE) },
    { MP_ROM_QSTR(MP_QSTR_FLAG_NOTIFY), MP_ROM_INT(MP_BLUETOOTH_CHARACTERISTIC_FLAG_NOTIFY) },
    { MP_ROM_QSTR(MP_QSTR_FLAG_INDICATE), MP_ROM_INT(MP_BLUETOOTH_CHARACTERISTIC_FLAG_INDICATE) },
    { MP_ROM_QSTR(MP_QSTR_FLAG_WRITE_NO_RESPONSE), MP_ROM_INT(MP_BLUETOOTH_CHARACTERISTIC_FLAG_WRITE_NO_RESPONSE) },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_bluetooth_globals, mp_module_bluetooth_globals_table);

const mp_obj_module_t mp_module_bluetooth = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_bluetooth_globals,
};

// This module should not be extensible (as it is not a CPython standard
// library nor is it necessary to override from the filesystem), however it
// has previously been known as `ubluetooth`, so by making it extensible the
// `ubluetooth` alias will continue to work.
MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_bluetooth, mp_module_bluetooth);

// ----------------------------------------------------------------------------
// Port API
// ----------------------------------------------------------------------------

typedef struct _ble_event_tuple_args {
    uint16_t event;
    const mp_int_t *numeric;
    size_t n_unsigned;
    size_t n_signed;
    const uint8_t *addr;
    const mp_obj_bluetooth_uuid_t *uuid;
    const uint8_t **data;
    uint16_t *data_len;
    size_t n_data;
    mp_obj_t *result;
} ble_event_tuple_args;

STATIC void invoke_irq_handler_protected(void *args_in) {
    ble_event_tuple_args *args = args_in;
    *args->result = mp_const_none;

    mp_obj_bluetooth_ble_t *o = MP_OBJ_TO_PTR(MP_STATE_VM(bluetooth));
    if (o->irq_handler == mp_const_none) {
        return;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_array_t mv_addr;
        mp_obj_array_t mv_data[2];
        assert(args->n_data <= 2);

        mp_obj_tuple_t *data_tuple = mp_local_alloc(sizeof(mp_obj_tuple_t) + sizeof(mp_obj_t) * MICROPY_PY_BLUETOOTH_MAX_EVENT_DATA_TUPLE_LEN);
        data_tuple->base.type = &mp_type_tuple;
        data_tuple->len = 0;

        for (size_t i = 0; i < args->n_unsigned; ++i) {
            data_tuple->items[data_tuple->len++] = MP_OBJ_NEW_SMALL_INT(args->numeric[i]);
        }
        if (args->addr) {
            mp_obj_memoryview_init(&mv_addr, 'B', 0, 6, (void *)args->addr);
            data_tuple->items[data_tuple->len++] = MP_OBJ_FROM_PTR(&mv_addr);
        }
        for (size_t i = 0; i < args->n_signed; ++i) {
            data_tuple->items[data_tuple->len++] = MP_OBJ_NEW_SMALL_INT(args->numeric[i + args->n_unsigned]);
        }
        #if MICROPY_PY_BLUETOOTH_ENABLE_CENTRAL_MODE
        if (args->uuid) {
            data_tuple->items[data_tuple->len++] = MP_OBJ_FROM_PTR(args->uuid);
        }
        #endif

        #if MICROPY_PY_BLUETOOTH_USE_GATTC_EVENT_DATA_REASSEMBLY
        void *buf_to_free = NULL;
        uint16_t buf_to_free_len = 0;
        if (args->event == MP_BLUETOOTH_IRQ_GATTC_NOTIFY || args->event == MP_BLUETOOTH_IRQ_GATTC_INDICATE || args->event == MP_BLUETOOTH_IRQ_GATTC_READ_RESULT) {
            if (args->n_data > 1) {
                // Fragmented buffer, need to combine into a new heap-allocated buffer
                // in order to pass to Python.
                // Only gattc_on_data_available calls this code, so data and data_len are writable.
                uint16_t total_len = 0;
                for (size_t i = 0; i < args->n_data; ++i) {
                    total_len += args->data_len[i];
                }
                uint8_t *buf = m_new(uint8_t, total_len);
                uint8_t *p = buf;
                for (size_t i = 0; i < args->n_data; ++i) {
                    memcpy(p, args->data[i], args->data_len[i]);
                    p += args->data_len[i];
                }
                args->data[0] = buf;
                args->data_len[0] = total_len;
                args->n_data = 1;
                buf_to_free = buf;
                buf_to_free_len = total_len;
            }
        }
        #endif

        for (size_t i = 0; i < args->n_data; ++i) {
            if (args->data[i]) {
                mp_obj_memoryview_init(&mv_data[i], 'B', 0, args->data_len[i], (void *)args->data[i]);
                data_tuple->items[data_tuple->len++] = MP_OBJ_FROM_PTR(&mv_data[i]);
            } else {
                data_tuple->items[data_tuple->len++] = mp_const_none;
            }
        }

        assert(data_tuple->len <= MICROPY_PY_BLUETOOTH_MAX_EVENT_DATA_TUPLE_LEN);

        mp_obj_bluetooth_ble_t *o = MP_OBJ_TO_PTR(MP_STATE_VM(bluetooth));
        *args->result = mp_call_function_2(o->irq_handler, MP_OBJ_NEW_SMALL_INT(args->event), MP_OBJ_FROM_PTR(data_tuple));

        #if MICROPY_PY_BLUETOOTH_USE_GATTC_EVENT_DATA_REASSEMBLY
        if (buf_to_free != NULL) {
            m_del(uint8_t, (uint8_t *)buf_to_free, buf_to_free_len);
        }
        #endif

        mp_local_free(data_tuple);

        nlr_pop();
    } else {
        // Uncaught exception, print it out.
        mp_printf(MICROPY_ERROR_PRINTER, "Unhandled exception in IRQ callback handler\n");
        mp_obj_print_exception(MICROPY_ERROR_PRINTER, MP_OBJ_FROM_PTR(nlr.ret_val));

        // Disable the BLE IRQ handler.
        o->irq_handler = mp_const_none;
    }
}

#ifndef MICROPY_PY_BLUETOOTH_SYNC_EVENT_STACK_SIZE
#define MICROPY_PY_BLUETOOTH_SYNC_EVENT_STACK_SIZE 4096
#endif

STATIC mp_obj_t invoke_irq_handler(uint16_t event,
    const mp_int_t *numeric, size_t n_unsigned, size_t n_signed,
    const uint8_t *addr,
    const mp_obj_bluetooth_uuid_t *uuid,
    const uint8_t **data, uint16_t *data_len, size_t n_data) {
    mp_obj_t result;
    ble_event_tuple_args args = {
        .result = &result,
        .event = event,
        .numeric = numeric,
        .n_unsigned = n_unsigned,
        .n_signed = n_signed,
        .addr = addr,
        .uuid = uuid,
        .data = data,
        .data_len = data_len,
        .n_data = n_data,
    };
    // On bare-metal (e.g STM32), this will just invoke the callback directly,
    // the host stack is already running in the scheduler. On ESP32, Unix, and
    // Zephyr, we are currently in RTOS/OS task context, so this will run the
    // callback on a MicroPython thread. If we're on an RTOS task, assume that
    // at most 1k bytes of stack has been used already on the host stack.
    mp_thread_run_on_mp_thread(invoke_irq_handler_protected, &args, MICROPY_PY_BLUETOOTH_SYNC_EVENT_STACK_SIZE - 1024);
    return result;
}

#define NULL_NUMERIC NULL
#define NULL_ADDR NULL
#define NULL_UUID NULL
#define NULL_DATA NULL
#define NULL_DATA_LEN NULL

void mp_bluetooth_gap_on_connected_disconnected(uint8_t event, uint16_t conn_handle, uint8_t addr_type, const uint8_t *addr) {
    mp_int_t args[] = {conn_handle, addr_type};
    invoke_irq_handler(event, args, 2, 0, addr, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_gap_on_connection_update(uint16_t conn_handle, uint16_t conn_interval, uint16_t conn_latency, uint16_t supervision_timeout, uint16_t status) {
    mp_int_t args[] = {conn_handle, conn_interval, conn_latency, supervision_timeout, status};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_CONNECTION_UPDATE, args, 5, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

#if MICROPY_PY_BLUETOOTH_ENABLE_PAIRING_BONDING
void mp_bluetooth_gatts_on_encryption_update(uint16_t conn_handle, bool encrypted, bool authenticated, bool bonded, uint8_t key_size) {
    mp_int_t args[] = {conn_handle, encrypted, authenticated, bonded, key_size};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_ENCRYPTION_UPDATE, args, 5, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

bool mp_bluetooth_gap_on_get_secret(uint8_t type, uint8_t index, const uint8_t *key, uint16_t key_len, const uint8_t **value, size_t *value_len) {
    mp_int_t args[] = {type, index};
    mp_obj_t result = invoke_irq_handler(MP_BLUETOOTH_IRQ_GET_SECRET, args, 2, 0, NULL_ADDR, NULL_UUID, &key, &key_len, 1);
    if (result == mp_const_none) {
        return false;
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(result, &bufinfo, MP_BUFFER_READ);
    *value = bufinfo.buf;
    *value_len = bufinfo.len;
    return true;
}

bool mp_bluetooth_gap_on_set_secret(uint8_t type, const uint8_t *key, size_t key_len, const uint8_t *value, size_t value_len) {
    mp_int_t args[] = { type };
    const uint8_t *data[] = {key, value};
    uint16_t data_len[] = {key_len, value_len};
    mp_obj_t result = invoke_irq_handler(MP_BLUETOOTH_IRQ_SET_SECRET, args, 1, 0, NULL_ADDR, NULL_UUID, data, data_len, 2);
    return mp_obj_is_true(result);
}

void mp_bluetooth_gap_on_passkey_action(uint16_t conn_handle, uint8_t action, mp_int_t passkey) {
    mp_int_t args[] = { conn_handle, action, passkey };
    invoke_irq_handler(MP_BLUETOOTH_IRQ_PASSKEY_ACTION, args, 2, 1, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}
#endif // MICROPY_PY_BLUETOOTH_ENABLE_PAIRING_BONDING

void mp_bluetooth_gatts_on_write(uint16_t conn_handle, uint16_t value_handle) {
    mp_int_t args[] = {conn_handle, value_handle};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_GATTS_WRITE, args, 2, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_gatts_on_indicate_complete(uint16_t conn_handle, uint16_t value_handle, uint8_t status) {
    mp_int_t args[] = {conn_handle, value_handle, status};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_GATTS_INDICATE_DONE, args, 3, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

mp_int_t mp_bluetooth_gatts_on_read_request(uint16_t conn_handle, uint16_t value_handle) {
    mp_int_t args[] = {conn_handle, value_handle};
    mp_obj_t result = invoke_irq_handler(MP_BLUETOOTH_IRQ_GATTS_READ_REQUEST, args, 2, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
    // Return non-zero from IRQ handler to fail the read.
    mp_int_t ret = 0;
    mp_obj_get_int_maybe(result, &ret);
    return ret;
}

void mp_bluetooth_gatts_on_mtu_exchanged(uint16_t conn_handle, uint16_t value) {
    mp_int_t args[] = {conn_handle, value};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_MTU_EXCHANGED, args, 2, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

#if MICROPY_PY_BLUETOOTH_ENABLE_L2CAP_CHANNELS
mp_int_t mp_bluetooth_on_l2cap_accept(uint16_t conn_handle, uint16_t cid, uint16_t psm, uint16_t our_mtu, uint16_t peer_mtu) {
    mp_int_t args[] = {conn_handle, cid, psm, our_mtu, peer_mtu};
    mp_obj_t result = invoke_irq_handler(MP_BLUETOOTH_IRQ_L2CAP_ACCEPT, args, 5, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
    // Return non-zero from IRQ handler to fail the accept.
    mp_int_t ret = 0;
    mp_obj_get_int_maybe(result, &ret);
    return ret;
}

void mp_bluetooth_on_l2cap_connect(uint16_t conn_handle, uint16_t cid, uint16_t psm, uint16_t our_mtu, uint16_t peer_mtu) {
    mp_int_t args[] = {conn_handle, cid, psm, our_mtu, peer_mtu};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_L2CAP_CONNECT, args, 5, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_on_l2cap_disconnect(uint16_t conn_handle, uint16_t cid, uint16_t psm, uint16_t status) {
    mp_int_t args[] = {conn_handle, cid, psm, status};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_L2CAP_DISCONNECT, args, 4, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_on_l2cap_send_ready(uint16_t conn_handle, uint16_t cid, uint8_t status) {
    mp_int_t args[] = {conn_handle, cid, status};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_L2CAP_SEND_READY, args, 3, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_on_l2cap_recv(uint16_t conn_handle, uint16_t cid) {
    mp_int_t args[] = {conn_handle, cid};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_L2CAP_RECV, args, 2, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}
#endif // MICROPY_PY_BLUETOOTH_ENABLE_L2CAP_CHANNELS

#if MICROPY_PY_BLUETOOTH_ENABLE_CENTRAL_MODE
void mp_bluetooth_gap_on_scan_complete(void) {
    invoke_irq_handler(MP_BLUETOOTH_IRQ_SCAN_DONE, NULL_NUMERIC, 0, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_gap_on_scan_result(uint8_t addr_type, const uint8_t *addr, uint8_t adv_type, const int8_t rssi, const uint8_t *data, uint16_t data_len) {
    mp_int_t args[] = {addr_type, adv_type, rssi};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_SCAN_RESULT, args, 1, 2, addr, NULL_UUID, &data, &data_len, 1);
}
#endif // MICROPY_PY_BLUETOOTH_ENABLE_CENTRAL_MODE

#if MICROPY_PY_BLUETOOTH_ENABLE_GATT_CLIENT
void mp_bluetooth_gattc_on_primary_service_result(uint16_t conn_handle, uint16_t start_handle, uint16_t end_handle, mp_obj_bluetooth_uuid_t *service_uuid) {
    mp_int_t args[] = {conn_handle, start_handle, end_handle};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_GATTC_SERVICE_RESULT, args, 3, 0, NULL_ADDR, service_uuid, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_gattc_on_characteristic_result(uint16_t conn_handle, uint16_t value_handle, uint16_t end_handle, uint8_t properties, mp_obj_bluetooth_uuid_t *characteristic_uuid) {
    // Note: "end_handle" replaces "def_handle" from the original version of this event.
    mp_int_t args[] = {conn_handle, end_handle, value_handle, properties};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_GATTC_CHARACTERISTIC_RESULT, args, 4, 0, NULL_ADDR, characteristic_uuid, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_gattc_on_descriptor_result(uint16_t conn_handle, uint16_t handle, mp_obj_bluetooth_uuid_t *descriptor_uuid) {
    mp_int_t args[] = {conn_handle, handle};
    invoke_irq_handler(MP_BLUETOOTH_IRQ_GATTC_DESCRIPTOR_RESULT, args, 2, 0, NULL_ADDR, descriptor_uuid, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_gattc_on_discover_complete(uint8_t event, uint16_t conn_handle, uint16_t status) {
    mp_int_t args[] = {conn_handle, status};
    invoke_irq_handler(event, args, 2, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

void mp_bluetooth_gattc_on_data_available(uint8_t event, uint16_t conn_handle, uint16_t value_handle, const uint8_t **data, uint16_t *data_len, size_t num) {
    mp_int_t args[] = {conn_handle, value_handle};
    invoke_irq_handler(event, args, 2, 0, NULL_ADDR, NULL_UUID, data, data_len, num);
}

void mp_bluetooth_gattc_on_read_write_status(uint8_t event, uint16_t conn_handle, uint16_t value_handle, uint16_t status) {
    mp_int_t args[] = {conn_handle, value_handle, status};
    invoke_irq_handler(event, args, 3, 0, NULL_ADDR, NULL_UUID, NULL_DATA, NULL_DATA_LEN, 0);
}

#endif // MICROPY_PY_BLUETOOTH_ENABLE_GATT_CLIENT

// ----------------------------------------------------------------------------
// GATTS DB
// ----------------------------------------------------------------------------

void mp_bluetooth_gatts_db_create_entry(mp_gatts_db_t db, uint16_t handle, size_t len) {
    mp_map_elem_t *elem = mp_map_lookup(db, MP_OBJ_NEW_SMALL_INT(handle), MP_MAP_LOOKUP_ADD_IF_NOT_FOUND);
    mp_bluetooth_gatts_db_entry_t *entry = m_new(mp_bluetooth_gatts_db_entry_t, 1);
    entry->data = m_new(uint8_t, len);
    entry->data_alloc = len;
    entry->data_len = 0;
    entry->append = false;
    elem->value = MP_OBJ_FROM_PTR(entry);
}

mp_bluetooth_gatts_db_entry_t *mp_bluetooth_gatts_db_lookup(mp_gatts_db_t db, uint16_t handle) {
    mp_map_elem_t *elem = mp_map_lookup(db, MP_OBJ_NEW_SMALL_INT(handle), MP_MAP_LOOKUP);
    if (!elem) {
        return NULL;
    }
    return MP_OBJ_TO_PTR(elem->value);
}

int mp_bluetooth_gatts_db_read(mp_gatts_db_t db, uint16_t handle, const uint8_t **value, size_t *value_len) {
    mp_bluetooth_gatts_db_entry_t *entry = mp_bluetooth_gatts_db_lookup(db, handle);
    if (entry) {
        *value = entry->data;
        *value_len = entry->data_len;
        if (entry->append) {
            entry->data_len = 0;
        }
    }
    return entry ? 0 : MP_EINVAL;
}

int mp_bluetooth_gatts_db_write(mp_gatts_db_t db, uint16_t handle, const uint8_t *value, size_t value_len) {
    mp_bluetooth_gatts_db_entry_t *entry = mp_bluetooth_gatts_db_lookup(db, handle);
    if (entry) {
        if (value_len > entry->data_alloc) {
            uint8_t *data = m_new_maybe(uint8_t, value_len);
            if (data) {
                entry->data = data;
                entry->data_alloc = value_len;
            } else {
                return MP_ENOMEM;
            }
        }

        memcpy(entry->data, value, value_len);
        entry->data_len = value_len;
    }
    return entry ? 0 : MP_EINVAL;
}

int mp_bluetooth_gatts_db_resize(mp_gatts_db_t db, uint16_t handle, size_t len, bool append) {
    mp_bluetooth_gatts_db_entry_t *entry = mp_bluetooth_gatts_db_lookup(db, handle);
    if (entry) {
        uint8_t *data = m_renew_maybe(uint8_t, entry->data, entry->data_alloc, len, true);
        if (data) {
            entry->data = data;
            entry->data_alloc = len;
            entry->data_len = 0;
            entry->append = append;
        } else {
            return MP_ENOMEM;
        }
    }
    return entry ? 0 : MP_EINVAL;
}

MP_REGISTER_ROOT_POINTER(mp_obj_t bluetooth);

#endif // MICROPY_PY_BLUETOOTH
