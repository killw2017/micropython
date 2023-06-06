#!/bin/bash

###################
# Script Functions
###################

function help {

    echo 'Micropython PSoC6 utility script' 
    echo 'usage: sh mpy-psoc6.sh <command>' 
    echo 'The available commands are:'
    echo ''
    echo '  quick-start          Setup of MicroPython IDE and PSoC6 board.'
    echo '                       Use this command for a guided installation and' 
    echo '                       quick start using MicroPython PSoC6.' 
    echo '                       usage: sh mpy-psoc6.sh quick-start [board [version]]'
    echo ''
    echo '                       board       PSoC6 prototyping kit name'
    echo '                       version     MicroPython PSoC6 firmware version'
    echo ''
    echo '  device-setup         Setup of MicroPython PSoC6 board.'
    echo '                       Use this command to install the deployment tools'
    echo '                       and MicroPython firmware binary, and deploy the'
    echo '                       firmware on the PSoC6 device.'
    echo '                       usage: sh mpy-psoc6.sh device-setup [board [[version [\q]]]'
    echo ''
    echo '                       board       PSoC6 prototyping kit name'
    echo '                       version     MicroPython PSoC6 firmware version'
    echo '                       \q          Quiet. Do not prompt any user confirmation request'
    echo ''
    echo '  firmware-deploy      Firmware deployment on MicroPython board.'
    echo '                       Use this command to deploy an existing .hex file'
    echo '                       on a PSoC6 board.'
    echo '                       Requires openocd available on the system path.'
    echo '                       usage: sh mpy-psoc6.sh firmware-deploy <board> <hex_file>'
    echo ''
    echo '                       board       PSoC6 prototyping kit name'
    echo '                       hex_file    MicroPython PSoC6 firmware .hex file'

}

function mpy_firmware_deploy {
    
    board=$1
    hex_file=$2

    echo Deploying Micropython...
    openocd -s openocd\scripts -c "source [find interface/kitprog3.cfg]; ; source [find target/psoc6_2m.cfg]; psoc6 allow_efuse_program off; psoc6 sflash_restrictions 1; program ${hex_file} verify reset exit;"

}

function mpy_firmware_download {

    board=$1
    version=$2

    echo Downloading MicroPython PSoC6 port ${version} for ${board} board...
    if [ "$version" = "latest" ]; then 
        sub_url="latest/download"
    else
        sub_url="download/${version}"
    fi
    curl -s -L https://github.com/infineon/micropython/releases/${sub_url}/mpy-psoc6_${board}.hex > mpy-psoc6_${board}.hex
   
}

function mpy_firmware_clean {

    board=$1

    echo Cleaning up micropython hex files...
    rm mpy-psoc6_${board}.hex

}

function openocd_download_install {

    echo Downloading openocd...
    curl -s -L https://github.com/Infineon/openocd/releases/download/release-v4.4.0/openocd-4.4.0.2134-linux.tar.gz > openocd.tar.gz
    echo Extracting openocd...
    tar -xf openocd.tar.gz
    export PATH=${PWD}/openocd/bin:$PATH
    ./openocd/udev_rules/install_rules.sh

}

function openocd_uninstall_clean {

    echo Cleaning up openOCD installation package...
    rm openocd.tar.gz
    rm -rf openocd

}

function mpy_device_setup {

    board=$1
    board_list=(CY8CPROTO-062-4343W)

    if [ "$board" = "" ]; then
        echo ''
        echo '      Supported MicroPython PSoC6 boards      '
        echo '+---------+-----------------------------------+'
        echo '|   ID    |              Board                |'
        echo '+---------+-----------------------------------+'
        echo '|   0     |  CY8CPROTO-062-4343W (default)    |'
        echo '+---------+-----------------------------------+'
        echo ''
        echo 'No user selection required. Only one choice.'
        
        board_index=0
        echo ''

        # Uncomment and remove preselection above when more options are available
        # echo Please type the desired board ID. 
        # read board_index

        board=${board_list[${board_index}]}
    fi  
    
    echo MicroPython PSoC6 Board  :: ${board}

    # Version selection
    mpy_firmware_version=$2

    if [  "$mpy_firmware_version" = "" ]; then 
        mpy_firmware_version=latest
    fi

    echo MicroPython PSoC6 Version :: ${mpy_firmware_version}
    echo ''

    # Download flashing tool and firmware
    openocd_download_install
    mpy_firmware_download ${board} ${mpy_firmware_version}

    if [ "$3" != "\q" ]; then
        echo ''
        echo Please CONNECT THE BOARD and PRESS ANY KEY to start the firmware deployment...
        read user_input
        echo ''
    fi

    # Deploy on board
    mpy_firmware_deploy ${board} mpy-psoc6_${board}.hex
    echo Device firmware deployment completed.   

    openocd_uninstall_clean
    mpy_firmware_clean ${board}

    if [ "$3" != "\q" ]; then
        echo ''
        echo Press any key to continue...
        read user_input
        echo ''
    fi
}

function arduino_lab_download_and_launch {

    echo Downloading Arduino Lab for Micropython...
    curl -s -L https://github.com/arduino/lab-micropython-editor/releases/download/0.5.0-alpha/Arduino.Lab.for.Micropython-linux_x64.zip > arduino-for-micropython.zip
    echo Extracting Arduino Lab for Micropython...
    mkdir arduino-lab-mpy
    unzip arduino-for-micropython.zip -d arduino-lab-mpy
    cd arduino-lab-mpy
    echo Launching Arduino Lab for Micropython...
    ./arduino-lab-micropython-ide
    cd ..

}

function arduino_lab_install_package_clean {

    echo Cleaning up Arduino Lab for Micropython installation package...
    rm arduino-for-micropython.zip

}

function mpy_quick_start {

    echo '################################################'
    echo '                Welcome to the                  '
    echo ' __  __ _            ___      _   _             '
    echo '|  \/  (_)__ _ _ ___| _ \_  _| |_| |_  ___ _ _'
    echo "| |\/| | / _| '_/ _ \  _/ || |  _| ' \/ _ \ ' \\"
    echo '|_|_ |_|_\__|_| \___/_|  \_, |\__|_||_\___/_||_|'
    echo '| _ \/ __| ___ / __|/ /  |__/'                    
    echo '|  _/\__ \/ _ \ (__/ _ \'                        
    echo '|_|  |___/\___/\___\___/' 
    echo ''
    echo '                 Quick Start                    '
    echo '################################################'

    mpy_device_setup $1 $2

    arduino_lab_download_and_launch

    arduino_lab_install_package_clean

    echo '################################################'
    echo '   The installation is completed. Have fun :)   '                 
    echo '################################################'

    echo ''
    echo Press any key to exit...
    read user_input
    echo ''

}

# Main script commands

case $1 in
   "quick-start")
        mpy_quick_start $2 $3
        ;;
    "device-setup")
        mpy_device_setup $2 $3 $4
        ;;
   "firmware-deploy")
        mpy_firmware_deploy $2 $3
        ;;
   "help")
        help
        ;;
   *)
        help
        ;;
esac
