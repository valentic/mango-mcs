#!/usr/bin/env Python

##########################################################################
#
#   Binary data snapshot base.
#
#   A number of the telemetry files use a similar approach
#   to packing values into binary files. The format consists
#   of a repeating number of records encoded using network
#   oriented byte-ordering and type sizes.
#
#   2008-08-13  Todd Valentic
#               Initial implementation
#
#   2010-06-01  Todd Valentic
#               Refactored to better handle versioned/variable formats.
#               Add getVersion() and setTimestamp() convienence methods.
#
#   2016-07-29  Todd Valentic
#               Use utcfromtimestmp for servers not running in UTC
#
#   2017-10-22  Todd Valentic
#               Add typeName
#
#   2018-04-20  Todd Valentic
#               Add balloon_id
#
##########################################################################

from datetime import datetime

import os
import bz2
import pytz
import struct
import ConfigParser

import model

class SnapshotBase:

    def __init__(self,typeName,data=None,balloon_id=None):

        self.typeName = typeName

        self.setBalloonId(balloon_id)

        if data:
            self.read(data)

    def getTypeName(self):
        return self.typeName

    def getVersion(self,data):
        return struct.unpack('!B',data[0])[0]

    def setTimestamp(self,secs):
        self.timestamp = datetime.utcfromtimestamp(secs).replace(tzinfo=pytz.utc)

    def getTimestamp(self):
        return self.timestamp

    def getBalloonId(self):
        return self.balloon_id

    def read(self,data,filename):
        self.values,data = self.parse(data,filename)
        return data

    def asConfig(self,section='values'):

        config = ConfigParser.ConfigParser()
        config.add_section(section)

        for section,values in self.values.items():
            config.add_section(section)

            for key,value in values.items():
                config.set(section,key,str(value))

        return config

    def asDict(self):
        return self.values

    def setBalloonId(self,id):
        self.balloon_id=id

    def setBalloonFromFilename(self,filename):
        if '_' in filename:
            sep = '_'
        else:
            sep = '-'

        imei = os.path.basename(filename).split(sep)[0]
        balloon = model.Balloon.query.filter_by(imei=imei).first()

        if not balloon:
            raise ValueError('Unknown balloon for IMEI %s from %s' % (imei,filename))

        self.setBalloonId(balloon.id)

    def parse(self,data,filename):
        # Returns a dictionary of dictionaries, remaining data
        # Filled in by child classes. Make sure to set the balloon_id.
        return {},data

    def unpack(self,format,data):
        size = struct.calcsize(format)
        rec = struct.unpack(format,data[0:size])
        return rec,data[size:]

def read(filename,snapshotFactory,**kw):

    snapshots = []

    if filename.endswith('bz2'):
        data = bz2.BZ2File(filename).read()
    else:
        data = open(filename).read()

    while data:
        snapshot = snapshotFactory(**kw)
        data = snapshot.read(data,filename)
        snapshots.append(snapshot)

    return snapshots

