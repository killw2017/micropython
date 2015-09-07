/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Damien P. George
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

#include "py/runtime.h"
#include "board.h"
#include "modpyb.h"
#include "ciaanxp_mphal.h"

#define RX_BUFFER_MAX_SIZE	2048

typedef struct _pyb_uart_obj_t {
    mp_obj_base_t base;
    uint32_t uartNumber;
    uint8_t bufferRx[RX_BUFFER_MAX_SIZE];
} pyb_uart_obj_t;

STATIC pyb_uart_obj_t pyb_uart_obj[] = {
    {{&pyb_uart_type}},
    {{&pyb_uart_type}},
};

void pyb_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    pyb_uart_obj_t *self = self_in;
    mp_printf(print, "UART");
}

STATIC mp_obj_t pyb_uart_make_new(mp_obj_t type_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    mp_int_t uart_id = mp_obj_get_int(args[0]);

	char aux[200]; sprintf(aux,"constructor. uart:%d",uart_id);
        Board_UARTPutSTR(aux);

    if (uart_id!=0 && uart_id!=3) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "UART %d does not exist", uart_id));
    }

    if(uart_id==0)
    {
	pyb_uart_obj[0].uartNumber=0;
    	return (mp_obj_t)&pyb_uart_obj[0];
    }
    pyb_uart_obj[1].uartNumber=3;
    return (mp_obj_t)&pyb_uart_obj[1];
}
//_______________________________________________________________________________________________________________



// Function init
/*
mp_obj_t pyb_uart_init(mp_obj_t self_in) {
    pyb_switch_obj_t *self = self_in;
    //return switch_get(SWITCH_ID(self)) ? mp_const_true : mp_const_false;
    //return Buttons_GetStatusByNumber(SWITCH_ID(self)-1) ? mp_const_true : mp_const_false;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyb_uart_value_obj, pyb_uart_init);
*/

STATIC mp_obj_t pyb_uart_init_helper(pyb_uart_obj_t *self, mp_uint_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_baudrate, MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = 9600} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_parity, MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_stop, MP_ARG_INT, {.u_int = 1} },
        { MP_QSTR_flow, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 100} },
        { MP_QSTR_timeout_char, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },
        { MP_QSTR_read_buf_len, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = RX_BUFFER_MAX_SIZE} },
    };

    // parse args
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    	Board_UARTPutSTR("Entro a INIT ");
	char aux[200];
        sprintf(aux,"baud:%d uart:%d",args[0].u_int,self->uartNumber);
        Board_UARTPutSTR(aux);

	// Baudrate
	mp_int_t baudrate = args[0].u_int;

	// bits
    	mp_int_t bits = args[1].u_int;
	if(bits!=8)
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "unsupported bits value"));

	// parity
	mp_int_t parity;
    	if (args[2].u_obj == mp_const_none) {
        	parity = 0;
    	} else {
        	parity = mp_obj_get_int(args[2].u_obj);
    	}

	// Stop bits
	mp_int_t stopBits = args[3].u_int;
	if(stopBits!=1 && stopBits!=2)
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "unsupported stop bits value"));

	// timeout
	uint32_t timeout = args[5].u_int;

	// buffer len
	uint32_t size = args[7].u_int;
	if(size > RX_BUFFER_MAX_SIZE)
		nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "buffer size is too big"));


	sprintf(aux,"baud:%d bits:%d par:%d stop:%d timeout:%d size:%d",baudrate,bits,parity,stopBits,timeout,size);
        Board_UARTPutSTR(aux);

	if(self->uartNumber==0) {
		mp_hal_rs485_setConfig(baudrate,stopBits,parity);
		mp_hal_rs485_setRxBuffer(self->bufferRx,size,timeout, 0);
	}
	else {
		mp_hal_rs232_setConfig(baudrate,stopBits,parity);
		mp_hal_rs232_setRxBuffer(self->bufferRx,size,timeout, 0);
	}

	// prueba envio luego de configurar
	//mp_hal_rs232_write("PRUEBA",6);

	return mp_const_none;
}

STATIC mp_obj_t pyb_uart_init(mp_uint_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    return pyb_uart_init_helper(args[0], n_args - 1, args + 1, kw_args);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(pyb_uart_init_obj, 1, pyb_uart_init);


//_______________________________________________________________________________________________________________

/*
mp_obj_t pyb_switch_call(mp_obj_t self_in, mp_uint_t n_args, mp_uint_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    return pyb_switch_value(self_in);
}
*/

STATIC const mp_map_elem_t pyb_uart_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_init), (mp_obj_t)&pyb_uart_init_obj },
};

STATIC MP_DEFINE_CONST_DICT(pyb_uart_locals_dict, pyb_uart_locals_dict_table);

const mp_obj_type_t pyb_uart_type = {
    { &mp_type_type },
    .name = MP_QSTR_UART,
    .print = pyb_uart_print,
    .make_new = pyb_uart_make_new,
    //.call = pyb_switch_call,
    .locals_dict = (mp_obj_t)&pyb_uart_locals_dict,
};
