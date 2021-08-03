#!/usr/bin/env python

###############################################################################
#
#   Relay control for USB RLY16 board
#
#   See https://www.robot-electronics.co.uk/htm/usb_rly16tech.htm
#
#   2019-04-28  Todd Valentic
#               Initial implementation
#
###############################################################################

import serial
import os
import datetime
import pytz

import lockfile

class RLY16(object):

    def __init__(self,device,timeout=1,log=None):
        self.device = device
        self.timeout = timeout

        basename = os.path.basename(device)  
        self.lockfile = lockfile.LockFile('/var/lock/LCK..%s' % basename,log=log)

    def __enter__(self):
        self.port = serial.Serial(self.device,19200,timeout=self.timeout)
        if not self.lockfile.acquire():
            raise IOError('Failed to acquire lock')
        return self

    def __exit__(self,type,value,traceback):
        self.port.close()
        self.lockfile.release()

    def command(self,cmd,payload=None,readBytes=0):

        output = chr(cmd)

        if payload is not None:
            output += chr(payload)

        self.port.write(output)
        self.port.flush()

        if readBytes:
            return self.port.read(readBytes)
        else:
            return 0
    
    def getStatus(self):
        
        timestamp = datetime.datetime.utcnow()
        timestamp = timestamp.replace(tzinfo=pytz.utc,microsecond=0)

        states = self.getStates()

        metadata = {}
        metadata['timestamp'] = str(timestamp)

        raw = {}
        raw['states'] = states

        relays = {}
        for bit in range(8):
            relays[str(bit+1)] = bool(states & 1<<bit)

        return dict(metadata=metadata,raw=raw,relays=relays)

    def getID(self):
        result = self.command(0x5A,readBytes=2)
        return [ord(x) for x in result]

    def getStates(self):
        return ord(self.command(0x5B,readBytes=1))

    def setStates(self,mask):
        self.command(0x5C,payload=mask)
        return self.getStates()

    def allOn(self):
        self.command(0x64)
        return self.getStates()
    
    def allOff(self):
        self.command(0x6E)
        return self.getStates()

    def turnOn(self,relay):
        self.command(0x65 + relay-1)
        return self.getStates()
    
    def turnOff(self,relay):
        self.command(0x6f + relay-1)
        return self.getStates()

if __name__ == '__main__':

    import logging
    logging.basicConfig(level=logging.INFO)

    with RLY16('/dev/relaybrd',timeout=10,log=logging) as rly16:
        print 'Starting'
        print 'ID        :',rly16.getID()
        print 'States    :',rly16.getStates()
        print 'Turn on 1 :',rly16.turnOn(1)
        print 'Turn off 1:',rly16.turnOff(1)
        print 'All on    :',rly16.allOn()
        print 'All off   :',rly16.allOff()
        print 'Set states:',rly16.setStates(0x02)

        print rly16.getStatus() 
    
    print 'Finished'


