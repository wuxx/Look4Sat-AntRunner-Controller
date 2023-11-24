#!/bin/bash


esptool.py --chip esp32c3 -p /dev/ttyACM0 -b 9600 erase_flash
