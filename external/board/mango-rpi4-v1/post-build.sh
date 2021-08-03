#!/bin/bash

##########################################################################
#
#   Install files into the target filesystem that are not part of the 
#   version controlled release, such as passwords and API keys which
#   should not be stored in public repositories.
#
#   It simply copies the contents of the secrets directory (specified as 
#   the first argument to this script) into the target.
#
#   2021-08-02  Todd Valentic
#               Initial release
#
##########################################################################

set -e

SECRETS_DIR=$1

if [ ! -d "$SECRETS_DIR" ]; then
    exit 0
]

rsync -av $SECRETS_DIR/ $TARGET_DIR

exit $?
