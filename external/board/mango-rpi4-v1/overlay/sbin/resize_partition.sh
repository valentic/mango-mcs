#!/bin/bash

######################################################################################	
#
#   Resize the root parition on new systems.
#
#   If the file /resizepartiion is present, resize the root parition to fill
#   the remaining space on the disk and resize the file system.
#
#   2021-07-07  Todd Valentic
#               Initial implementation
#
######################################################################################	

set -e

FLAGFILE=/resize_root
DEVICE=/dev/mmcblk0
PARTITION=${DEVICE}p2

if ! id | grep -q root; then
    echo "must be run as root"
    exit 1
fi

if [ ! -f $FLAGFILE ]; then
    exit 0
fi

echo "Resizing parition (ignore the re-reading error message)"
echo ", +" | sfdisk -q -N 2 --no-reread $DEVICE

echo "Reloading parition information"
partx -u $DEVICE

echo "Resizing filesystem"
resize2fs $PARTITION

rm $FLAGFILE

exit 0
