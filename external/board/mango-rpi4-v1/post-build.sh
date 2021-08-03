#!/bin/bash

##########################################################################
#
#   Install files into the target filesystem that are not part of the 
#   version controlled release, such as passwords and API keys which
#   should not be stored in public repositories.
#
#   It simply copies the contents of the secrets directory to the target. 
#   The location of the secrets directory is given in command line arguments
#   as the secretdir=xxx option. Note that a set of options is passed to
#   all of the post-*.sh scripts, so the parameters will contain values
#   that are not relevant to us. Search through them for the secretdir
#   parameter and only copy when it is found.
#
#   2021-08-02  Todd Valentic
#               Initial release
#
#   2021-08-03  Todd Valetn
#
##########################################################################

set -e

SECRETS_DIR=

for arg in "$@"; do

	case "${arg}" in
		secretsdir=*)
            SECRETS_DIR="${arg:11}"
		    ;;
	esac

done

if [ -z "$SECRETS_DIR" ]; then
    exit 0
fi

if [ ! -d "$SECRETS_DIR" ]; then
    echo "The secrets directory does not exist: $SECRETS_DIR"
    exit 1
fi

rsync -av $SECRETS_DIR/ $TARGET_DIR

exit $?

