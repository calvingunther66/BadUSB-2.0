#!/bin/bash
export PICO_SDK_PATH=~/pico/pico-sdk
cd ~/BadUSB-2.0/vgm_firmware
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
rm -rf build
mkdir build
cd build
cmake ..
make -j4
echo "BUILD COMPLETE. Check for badusb2_vgm.uf2"
