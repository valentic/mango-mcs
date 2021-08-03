#!/usr/bin/env python

###################################################################
#
#   PDU Data Monitor
#
#   Read PDU telemetry
#
#   2018-05-21  Todd Valentic
#               Initial implementation
#
#   2019-08-06  Todd Valentic
#               Add keep state (don't set)
#
###################################################################

from DataMonitor import DataMonitor

import sys
import struct

from pdulib import PDU

class PDUMonitor(DataMonitor):

    def __init__(self,argv):
        DataMonitor.__init__(self,argv)

        self.pductl = self.connect('pductl')
        self.unit   = self.get('pdu.host','pdu1')
        self.control = self.getboolean('pdu.control',True)

        if self.unit not in self.pductl.list():
            self.exit('Exiting - PDU %s not configured' % self.unit)

        self.log.info('Ready to start')

    def goingOffToOn(self):

        if not self.control:
            self.on = True
            return

        rails = self.pductl.list()[self.unit]['rails']
        rails = { k: v for k,v in rails.items() if v['stages'] }
        rails = { k: v for k,v in rails.items() if v['enabled'] }

        curStage = {}

        while self.wait(1):

            self.log.debug('rails: %s' % rails)

            for railName,rail in rails.items():
                stage = rail['stages'][0]
                stageName = stage['name']
                state = stage['state']

                if railName not in curStage or curStage[railName]!=stageName:
                    self.log.info('Change: %s[%s] -> %s (%s)' % (self.unit,railName,stageName,state))
                    curStage[railName]=stageName
                    if state!='keep':
                        self.pductl.cmd(self.unit,'SP',railName,state)

                stage['duration'] -= 1

                if stage['duration'] <= 0:
                    rail['stages'].pop(0)

                    if not rail['stages']:
                        del rails[railName]

            if not rails:
                self.log.info('All rails are up')
                break

        self.on = True

    def goingOnToOff(self):

        if not self.control:
            self.on = False
            return

        rails = self.pductl.list()[self.unit]

        for railName,rail in rails.items():
            state   = rail['stopState']
            enabled = rail['enabled']
            label   = rail['label']

            if enabled and state!='keep':
                self.pductl.cmd(self.unit,'SP',railName,state)
                self.log.info('Set %s to %s' % (label,state))

        self.on = False

    def sample(self):

        self.log.info('Sampling')

        try:
            data = self.pductl.status(self.unit)
        except:
            self.log.exception('Problem reading from PDU')
            return None

        self.putCache(self.unit,data)
        self.log.debug(data)

        return data 

    def write(self,output,timestamp,data):

        version = 1

        output.write(struct.pack('!B',version))
        output.write(struct.pack('!i',timestamp))
        output.write(struct.pack('!8B',*data['RH']))
        output.write(struct.pack('!11B',*data['RS']))
        output.write(struct.pack('!20f',*data['RT']))
        output.write(struct.pack('!4f',*data['RBME']))
        output.write(struct.pack('!12f',*data['RLTC']['volts']))
        output.write(struct.pack('!12f',*data['RLTC']['amps']))

if __name__ == '__main__':
    PDUMonitor(sys.argv).run()
