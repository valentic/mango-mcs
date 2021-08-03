#!/usr/bin/env python

#####################################################################
#
#   Ettus GPSDO Interface
#
#   Provides a high-level interface to the GPS hardware.
#   Acts as a compatible replacement for the OBuoy GPS
#   code. Simply forces the compass readings to be 0.
#
#   2012-01-13  Todd Valentic
#               Initial implementation. Based on OBuoy GPS.
#
#   2012-02-06  Todd Valentic
#               Preset compass values from file. Default location is
#                   in /transmit/share/gps.conf. Format is ".ini" and
#                   options are in the section named 'compass':
#
#                       [compass]
#
#                       heading: degs
#                       pitch:   degs
#                       roll:    degs
#
#   2016-07-13  Todd Valentic
#               Adapted for Ettus code.
#
#####################################################################

import sys
import os
import datetime
import commands
import ExtendedConfigParser

import logging

def format(position):

    result = []
    result.append("%f\t\tLatitude (N is positive)" % position['latitude'])
    result.append("%f\t\tLongitude (E is positive)" % position['longitude'])
    result.append("%f\t\tHeading" % position['heading'])
    result.append("%f\t\tPitch" % position['pitch'])
    result.append("%f\t\tRoll" % position['roll'])
    result.append("%f\t\tSpeed" % position['speed'])

    return '\n'.join(result)

class GPS:

    def __init__(self,device,maxAttempts=12,**kw):
        self.device = device

        self.compass = [0.0]*6
        self.position = None
        self.maxAttempts = maxAttempts
        self.configFile = '/transmit/share/gps.conf'

        if os.path.exists(self.configFile):
            self.setup()

    def close(self):
        pass

    def setup(self):

        data = ExtendedConfigParser.ExtendedConfigParser()
        data.read(self.configFile)

        self.compass[3] = data.getfloat('compass','heading','NaN')
        self.compass[4] = data.getfloat('compass','pitch','NaN')
        self.compass[5] = data.getfloat('compass','roll','NaN')

    def getData(self):

        try:
            fields = self.readFields()
        except:
            logging.exception('Problem in readFields')
            return

        for field in fields:
            data = field.split(',')
            if data[0] == '$GPRMC' and data[2] == 'A':
                self.position = data

    def getSuite(self):

        self.position = None

        for attempt in range(self.maxAttempts):
            self.getData()
            if (self.compass and self.position):
                break

        if not (self.compass and self.position):
            raise IOError("No Valid GPS data")

        return self.cookFields()

    def cookFields(self):

        results = {}

        if self.position:
            D = self.position[9] # date
            T = self.position[1] # time
            results['timestamp'] = [int(D[4:])+2000,
                                    int(D[2:4]),
                                    int(D[0:2]),
                                    int(T[0:2]),
                                    int(T[2:4]),
                                    int(T[4:6])]
            results['latitude']  = self.convertDegrees(self.position[3],self.position[4],2)
            results['longitude'] = self.convertDegrees(self.position[5],self.position[6],3)
            results['speed']     = float(self.position[7])

        if self.compass:
            results['heading'] = float(self.compass[3])

            if self.compass[4] is not None:
                results['pitch'] = float(self.compass[4])
            else:
                results['pitch'] = float('NaN')
            if self.compass[5] is not None:
                results['roll'] = float(self.compass[5])
            else:
                results['roll'] = float('NaN')

        return results

    def convertDegrees(self,magnitude,direction,size):

        degrees = float(magnitude[0:size]) + float(magnitude[size:])/60

        if (direction == 'W' or direction == 'S'):
            degrees *= -1

        return degrees

    def setClock(self):

        self.position = None

        for attempt in range(self.maxAttempts):
            self.getData()
            if self.position:
                break

        if not self.position:
            raise IOError("No Valid GPS data")

        gpsTime = datetime.datetime(*self.cookFields()['timestamp'])
        cmd = gpsTime.strftime('setclock %m%d%H%M%Y.%S')
        status,output = commands.getstatusoutput(cmd)

        if status!=0:
            raise IOError(output)

        return cmd

    def readFields(self):

        cmd = 'sudo /usr/libexec/uhd/query_gpsdo_sensors --args="addr=%s"' % self.device

        status,output = commands.getstatusoutput(cmd)
        if status!=0:
            raise IOError("Error reading Ettus GPSDO")

        results = []

        for line in output.split('\n'):
            if line.startswith('GPS_'):
                results.append(line.split(': ',1)[1])

        #fields = response.split(',')

        return results

if __name__ == '__main__':

    if len(sys.argv)<3:
        print 'Usage: gps.py <devive> clock|position'
        sys.exit(1)

    device = sys.argv[1]
    command = sys.argv[2]
    gps = GPS(device)

    if command == 'position':
        result = gps.getSuite()
        print format(result)

    if command == 'clock':
        print "Ran %s" % gps.setClock()

    sys.exit(0)

