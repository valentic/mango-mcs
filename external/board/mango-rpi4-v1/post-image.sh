#!/bin/bash

# Temporary fix to add entries to config.txt. Can be removed if this patch is ever accepted:
# https://patchwork.ozlabs.org/project/buildroot/patch/20181204160254.15785-1-gael.portay@collabora.com/

set -e

for arg in "$@"
do
	case "${arg}" in
		--*=*)
        # Set option=value
        optval="${arg:2}"
        if ! grep -qE "^${optval}" "${BINARIES_DIR}/rpi-firmware/config.txt"; then
            echo "Adding ${optval} to config.txt"
            cat << __EOF__ >> "${BINARIES_DIR}/rpi-firmware/config.txt"
$optval
__EOF__
        fi
		;;
	esac

done

exit $?
