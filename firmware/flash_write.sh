#!/bin/bash

esptool.py -p /dev/ttyACM0 \
        -b 460800 \
        --before default_reset \
        --after hard_reset \
        --chip esp32c3  \
        write_flash \
        --flash_mode dio \
        --flash_size 2MB \
        --flash_freq 80m \
        0x0 bootloader.bin \
        0x8000 partition-table.bin \
        0x10000 protocol-bridge.bin
