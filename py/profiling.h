/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) SatoshiLabs
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

#ifndef MICROPY_INCLUDED_PY_PROFILING_H
#define MICROPY_INCLUDED_PY_PROFILING_H

#include "py/objtype.h"
#include "py/objgenerator.h"
#include "py/objfun.h"
#include "py/bc.h"

#if MICROPY_PY_SYS_SETTRACE

#define prof_is_executing MP_STATE_THREAD(prof_callback_is_executing)

// This is the implementation for the sys.settrace
mp_obj_t prof_settrace(mp_obj_t callback);

// For every VM instruction tick this function deduces events from the state.
mp_obj_t prof_instr_tick(mp_code_state_t *code_state, bool is_exception);


typedef struct _mp_obj_code_t {
    mp_obj_base_t base;
    const mp_raw_code_t *rc;
    mp_obj_dict_t *dict_locals;
    mp_obj_t lnotab;
} mp_obj_code_t;

typedef struct _mp_obj_frame_t {
    mp_obj_base_t base;
    const mp_code_state_t *code_state;
    struct _mp_obj_frame_t *back;
    mp_obj_t callback;
    mp_obj_code_t *code;
    mp_uint_t lasti;
    mp_uint_t lineno;
    bool trace_opcodes;
} mp_obj_frame_t;

mp_obj_t mp_obj_new_code(const mp_raw_code_t *rc);
mp_obj_t mp_obj_new_frame(const mp_code_state_t *code_state);

mp_obj_t prof_frame_enter(mp_code_state_t *code_state);
mp_obj_t prof_frame_update(const mp_code_state_t *code_state);

void prof_extract_prelude(const byte *bytecode, mp_bytecode_prelude_t *prelude);

#endif // MICROPY_PY_SYS_SETTRACE
#endif // MICROPY_INCLUDED_PY_PROFILING_H
