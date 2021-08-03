#!/usr/bin/env python

##########################################################################
#
#   GPS Data Record
#
#   Format - Binary:
#
#   0    !B  message type (1)
#   1    !B  version
#   2    !B  sats
#   3    !B  mode
#   4    !i  timestamp
#   5    !f  latitude
#   6    !f  longitude
#   7    !f  altitude
#   8    !f  speed
#   9    !f  track
#   10   !f  climb
#
#   2017-10-22  Todd Valentic
#               Initial implementation
#
#   2018-04-20  Todd Valentic
#               Set balloon_id
#
#   2018-05-24  Todd Valentic
#               Only parse filename for balloon_id if it isn't set
#
##########################################################################

import binarydata
import struct

msgType = 1

class Snapshot(binarydata.SnapshotBase):

    def __init__(self,*args,**kw):
        binarydata.SnapshotBase.__init__(self,'gps',*args,**kw)

    def parse(self,buffer,filename):

        version = self.getVersion(buffer)

        if not self.balloon_id:
            self.setBalloonFromFilename(filename)

        if version==1:
            return self.parseVersion1(buffer)
        else:
            return ValueError('Unknown version: %s' % version)

    def parseVersion1(self,buffer):
        format = '!4Bi6f'
        rec,buffer = self.unpack(format,buffer)

        return self.parseValues(rec),buffer

    def parseValues(self,rec):

        self.setTimestamp(rec[4])
        timestr = self.timestamp.strftime('%Y%m%d-%H:%M:%S')

        values = {}

        values['msgType']   = rec[0]
        values['version']   = rec[1]
        values['sats']      = rec[2]
        values['mode']      = rec[3]
        values['timestr']   = timestr
        values['timestamp'] = rec[4]
        values['latitude']  = rec[5]
        values['longitude'] = rec[6]
        values['altitude']  = rec[7]
        values['speed']     = rec[8]
        values['track']     = rec[9]
        values['climb']     = rec[10]

        return values

def read(filename,**kw):
    return binarydata.read(filename,Snapshot,**kw)

if __name__ == '__main__':

    import sys
    filename = sys.argv[1]

    snapshots = read(filename)

    print 'Loaded %d snapshots' % len(snapshots)

    for count,snapshot in enumerate(snapshots):
        print 'Snapshot %d' % count
        print snapshot.asDict()


