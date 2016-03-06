#! /bin/bash
# Script run with root permission to load the nvram_uio driver

# Remove any existing driver
rmmod nvram_uio > /dev/null

# Since load with insmod, the dependant modules are not loaded automatically
modprobe uio

insmod driver/nvram_uio.ko

# Allow user access to the nvram_uio device
sleep 1
chmod 666 /dev/uio0
