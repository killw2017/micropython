#!/usr/bin/env bash

set -x

make ${MAKEOPTS} -C ports/cc3200 BTARGET=application BTYPE=release
make ${MAKEOPTS} -C ports/cc3200 BTARGET=bootloader  BTYPE=release
