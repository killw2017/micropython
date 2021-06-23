#!/bin/bash

# function for building firmware
function do_build() {
    descr=$1
    board=$2
    shift
    shift
    echo "building $descr $board"
    build_dir=/tmp/rp2-build-$board
    $MICROPY_AUTOBUILD_MAKE $@ BOARD=$board BUILD=$build_dir || exit 1
    mv $build_dir/firmware.hex $dest_dir/$descr$fw_tag.hex
    rm -rf $build_dir
}

# check/get parameters
if [ $# != 2 ]; then
    echo "usage: $0 <fw-tag> <dest-dir>"
    exit 1
fi

fw_tag=$1
dest_dir=$2

# check we are in the correct directory
if [ ! -r modmimxrt.c ]; then
    echo "must be in rp2 directory"
    exit 1
fi

# build the boards
do_build Teensy_4.0 TEENSY40
do_build Teensy_4.1 TEENSY41
