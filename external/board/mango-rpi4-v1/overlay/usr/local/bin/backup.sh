#!/bin/sh

FILENAME=$(date +%Y%m%d-%H%M%S)-backup-$(hostname -s).tgz

tar zcvf ${FILENAME} \
    --exclude "*/env/*" \
    --exclude "station.conf" \
    --exclude "*.pyc" \
    backup.sh \
    /etc \
    /usr/local \
    /opt/transport/groups \
    /opt/rsync-mirror \
    /transmit/schedules \
    /transmit/share

