#!/usr/bin/env python2

###################################################################
#
#   GPS Data Monitor
#
#   Read current position from gpsd. 
#
#   2017-10-11  Todd Valentic
#               Initial implementation
#
#   2019-06-10  Todd Valentic
#               Use new cache methods
#               Add altitude
#
#   2021-06-23  Todd Valentic
#               Set cache timeout
#
#   2021-07-17  Todd Valentic
#               Add ability to save if position changes
#
###################################################################

from DataMonitor import DataMonitor
from Transport.Util import datefunc

from math import radians, cos, sin, asin, sqrt

import sys
import struct
import gpsd
import json
import os

def haversine(lat1, lon1, lat2, lon2):

    R = 3959.87433 # miles.  For Earth radius in kilometers use 6372.8 km

    dLat = radians(lat2 - lat1)
    dLon = radians(lon2 - lon1)
    lat1 = radians(lat1)
    lat2 = radians(lat2)

    a = sin(dLat/2)**2 + cos(lat1)*cos(lat2)*sin(dLon/2)**2
    c = 2*asin(sqrt(a))

    return R * c

OUTPUT_CHANGED='change'
OUTPUT_ALWAYS='always'

class GPSMonitor(DataMonitor):

    def __init__(self,argv):
        DataMonitor.__init__(self,argv)

        cache_timeout = self.getTimeDelta('cache.timeout')

        # always or change
        self.output_mode = self.get('output.mode',OUTPUT_ALWAYS)

        self.change_limit = self.getfloat('change.limit',20)
        self.cache_filename = 'position.dat'

        if cache_timeout:
            secs = datefunc.timedelta_as_seconds(cache_timeout)
            self.cacheService.set_timeout('gps',secs)
            self.log.info('Setting cache timeout to %s' % cache_timeout)

        self.prev_position = self.load_position()

        self.log.info('Ready to start')

    def load_position(self): 

        try:
            with open(self.cache_filename) as f:
                return json.loads(f.read())
        except:
            self.log.info('No previous position file') 
            return None 

    def save_position(self, data):
            
        try:    
            with open(self.cache_filename,'w') as f:
                f.write(json.dumps(vars(data), default=str))
        except:
            self.log.exception('Failed to save current position file')

    def update(self,data):
        # Add compatible field names for schedules
        data['latitude'] = data['lat']
        data['longitude'] = data['lon']
        data['altitude'] = data['alt']
        data['timestamp'] = data['time']
        self.putCache('gps',data)

    def has_moved(self, data):

        if data.mode==3:

            if self.prev_position:

                lat1 = data.lat
                lon1 = data.lon
                lat2 = self.prev_position['lat']
                lon2 = self.prev_position['lon']

                dist = haversine(lat1, lon1, lat2, lon2)

                self.log.debug('Moved %f m' % dist)

                return dist > self.change_limit

            else:
                return True

        return False

    def sample(self):

        try:
            gpsd.connect()
            packet = gpsd.get_current()
        except:
            self.log.exception('Problem reading from gpsd')
            return None

        self.update(vars(packet))
        self.log.info(packet)

        if self.output_mode == OUTPUT_CHANGED: 
            if self.has_moved(packet):
                self.log.info('Change detected')
                self.save_position(packet)
                self.prev_position = self.load_position()
                return packet
            else:
                return None
        else:
            return packet

    def write(self,output,timestamp,data):

        version = 1

        output.write(struct.pack('!B',version))
        output.write(struct.pack('!B',data.sats))
        output.write(struct.pack('!B',data.mode))
        output.write(struct.pack('!i',timestamp))
        output.write(struct.pack('!f',data.lat))
        output.write(struct.pack('!f',data.lon))
        output.write(struct.pack('!f',data.alt))
        output.write(struct.pack('!f',data.hspeed))
        output.write(struct.pack('!f',data.track))
        output.write(struct.pack('!f',data.climb))

if __name__ == '__main__':
    GPSMonitor(sys.argv).run()
