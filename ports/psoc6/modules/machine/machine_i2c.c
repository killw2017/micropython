// std includes
#include <stdio.h>
#include <string.h>


// mpy includes
#include "extmod/machine_i2c.h"
#include "py/runtime.h"
#include "py/mphal.h"


// MTB includes
#include "cybsp.h"


// port-specific includes
#include "drivers/machine/psoc6_gpio.h"
#include "drivers/machine/psoc6_i2c.h"
#include "modmachine.h"
#include "mplogger.h"
#include "pins.h"


#define DEFAULT_I2C_FREQ     (400000)
#define MICROPY_HW_I2C_SCL   (CYBSP_I2C_SCL)
#define MICROPY_HW_I2C_SDA   (CYBSP_I2C_SDA)
#define PSOC_I2C_MASTER_MODE (CYHAL_I2C_MODE_MASTER)


extern mp_hal_pin_obj_t mp_hal_get_pin_obj(mp_obj_t obj);


STATIC machine_i2c_obj_t machine_i2c_obj;


STATIC void machine_i2c_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "I2C(%u, freq=%u, scl=%u, sda=%u)", self->i2c_id, self->freq, self->scl, self->sda);
}


mp_obj_t machine_i2c_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    mplogger_print("%q constructor invoked\n", MP_QSTR_I2C);
    enum { ARG_id, ARG_freq, ARG_scl, ARG_sda };

    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id, MP_ARG_REQUIRED | MP_ARG_OBJ },
        { MP_QSTR_freq, MP_ARG_INT, {.u_int = DEFAULT_I2C_FREQ} },
        { MP_QSTR_scl, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_sda, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };

    // Parse args.
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Get I2C bus.
    int i2c_id = mp_obj_get_int(args[ARG_id].u_obj);

    if (i2c_id != PSOC_I2C_MASTER_MODE) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("I2C id '%d' not supported !"), i2c_id);
    }

    // Get peripheral object
    // TODO: an individual machine_i2c_obj_t::i2c_obj is required per I2C bus !
    machine_i2c_obj_t *self = (machine_i2c_obj_t *)&machine_i2c_obj;
    self->base.type = &machine_i2c_type;
    self->i2c_id = i2c_id;

    // get scl & Sda pins & configure them
    if (args[ARG_scl].u_obj != mp_const_none) {
        int scl = mp_hal_get_pin_obj(args[ARG_scl].u_obj);

        if (self->scl == -1) {
            size_t slen;
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("SCL pin (%s) not found !"), mp_obj_str_get_data(args[ARG_scl].u_obj, &slen));
        }

        self->scl = scl;
    } else {
        self->scl = MICROPY_HW_I2C_SCL;
    }


    if (args[ARG_sda].u_obj != mp_const_none) {
        int sda = mp_hal_get_pin_obj(args[ARG_sda].u_obj);

        if (self->sda == -1) {
            size_t slen;
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("SDA pin (%s) not found !"), mp_obj_str_get_data(args[ARG_sda].u_obj, &slen));
        }
        self->sda = sda;
    } else {
        self->sda = MICROPY_HW_I2C_SDA;
    }

    self->freq = args[ARG_freq].u_int;

    // initialise I2C Peripheral and configure as master
    cy_rslt_t result = i2c_init(self);

    if (result != CY_RSLT_SUCCESS) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("I2C initialisation failed with return code %lx !"), result);
    }
    return MP_OBJ_FROM_PTR(self);
}


STATIC int machine_i2c_transfer(mp_obj_base_t *self_in, uint16_t addr, size_t len, uint8_t *buf, unsigned int flags) {
    machine_i2c_obj_t *self = MP_OBJ_TO_PTR(self_in);
    cy_rslt_t result = CY_RSLT_SUCCESS;
    bool send_stop = (flags & MP_MACHINE_I2C_FLAG_STOP) ? true : false;
    uint32_t timeout = 0;

    if ((flags & MP_MACHINE_I2C_FLAG_READ) == MP_MACHINE_I2C_FLAG_READ) {
        result = i2c_read(&self->i2c_obj, addr, buf, len, timeout, send_stop);

        if (result != CY_RSLT_SUCCESS) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("cyhal_i2c_master_read failed with return code %lx !"), result);
        }

        return len;
    } else {
        // handle scan type bus checks and send stop
        if (buf == NULL) {
            result = i2c_write(&self->i2c_obj, addr, buf, 0, 50, send_stop);

            if ((result != CY_RSLT_SUCCESS)) {
                // these 2 errors occur if nothing is attached to sda/scl, but they are pulled-up (0xaa2004) or not pulled-up (0xaa2003).
                // In the latter case, due to not reaction at all the timeout has to expire. Latency is therefore high.
                if (result != 0xaa2004 && result != 0xaa2003) {
                    mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("cyhal_i2c_master_write failed with return code %lx !"), result);
                }

                return 1;
            }

            return len;
        } else {
            result = i2c_write(&self->i2c_obj, addr, buf, len, timeout, send_stop);

            if (result != CY_RSLT_SUCCESS) {
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("cyhal_i2c_master_write failed with return code %lx !"), result);
            }
        }

        return len;
    }
}


STATIC const mp_machine_i2c_p_t machine_i2c_p = {
    .transfer_single = machine_i2c_transfer,
    .transfer = mp_machine_i2c_transfer_adaptor
};


MP_DEFINE_CONST_OBJ_TYPE(
    machine_i2c_type,
    MP_QSTR_I2C,
    MP_TYPE_FLAG_NONE,
    make_new, machine_i2c_make_new,
    print, machine_i2c_print,
    protocol, &machine_i2c_p,
    locals_dict, &mp_machine_i2c_locals_dict
    );
