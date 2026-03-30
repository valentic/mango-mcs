#!/bin/bash -ex

if [ $(id -u) -eq 0 ]; then
	set +x
	echo ""
	echo "This script should not be run as root!"
	echo ""
	echo "Run this script again as a normal user. Note that the buildroot-ts/"
	echo "directory should be owned by the same user (or have adequate"
	echo "permissions) that is running this Docker container build script!"
	echo ""
	exit 1;
fi


podman build --quiet --tag "buildroot-buildenv-$(git rev-parse --short=12 HEAD)" $(dirname "$0")/../docker/

# build must run as a normal user
# Overwrite $HOME to allow for buildroot ccache to work somewhat seamlessly
podman run --rm -it --userns=keep-id --volume $(pwd):/work -w /work -e HOME=/work "buildroot-buildenv-$(git rev-parse --short=12 HEAD)" $@
