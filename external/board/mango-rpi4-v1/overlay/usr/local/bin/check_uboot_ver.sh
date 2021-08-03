#!/bin/bash

#####################################################################
#
#   On BeagleBone systems, check the version of uBoot on the internal 
#   flash and make sure that it is the same as the one used on the 
#   SD card. If they differ, update and reboot.
#
#   2019-06-01  Todd Valentic
#               Initial implementation 
#
#####################################################################

set -e

if [ $UID != 0 ]; then
    echo "Must be run as root"
    exit 1
fi

PATH=$PATH:/opt/scripts/tools

temp=$( version.sh )

function getVersion() { 
   echo "$temp" | grep bootloader | grep $1 | awk -F ':' '{print $4}'
}

function getDevice() {
   echo "$temp" | grep bootloader | grep $1 | awk -F ':' '{print $3}' | tr -d "\[\]"
}

CARD_VER=$( getVersion microSD )
EMMC_VER=$( getVersion eMMC )

echo "microSD: $CARD_VER"
echo "eMMC   : $EMMC_VER"

if [ "$CARD_VER" != "$EMMC_VER" ]; then
    target=$( getDevice eMMC )
    echo "Version mismatch. Updating eMMC bootloader on $target"
    dd if=/opt/backup/uboot/MLO of=$target count=1 seek=1 bs=128k
    dd if=/opt/backup/uboot/u-boot.img of=$target count=2 seek=1 bs=384k 
fi

exit 0

