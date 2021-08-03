#!/usr/bin/env python2

###################################################################
#
#   SBC control service
#
#   This service is used to access the DIO and ADC ports.
#   The heavy lifting is done through the sbcctl program.
#
#   2016-07-13  Todd Valentic
#               Initial implementation. Based on TS2760 sbcctl
#
#   2016-07-18  Todd Valentic
#               Add reading CPU temp sensors
#
###################################################################

from Transport  import ProcessClient
from Transport  import XMLRPCServerMixin

import sys
import commands

class Server(ProcessClient,XMLRPCServerMixin):

    def __init__(self,args):
        ProcessClient.__init__(self,args)
        XMLRPCServerMixin.__init__(self)

        self.sbcctlCmd = self.get('command.sbcctl','/bin/sbcctl')

        self.register_function(self.status)
        self.register_function(self.setpin)
        self.register_function(self.reset)
        self.register_function(self.gpio)
        self.register_function(self.device)

        self.cache = self.connect('cache')

        while self.wait(5):
            try:
                self.status()
                break
            except:
                self.log.exception('Problem')
                self.log.info('Waiting for device to be ready')

    def status(self,*pos,**kw):
        results = self.sbcctl('status')
        #results += self.readSensors()
        self.cache.put('sbcctl',results)
        return results

    def setpin(self,pin,state):
        return self.sbcctl('setpin',pin,state)

    def reset(self,pin,state):
        return self.sbcctl('reset')

    def gpio(self,*pos):
        return self.sbcctl('gpio',*pos)

    def device(self,name,state):
        return self.sbcctl('device',name,state)

    def sbcctl(self,*pos):
        return self.runCommand(self.sbcctlCmd,*pos)

    def readSensors(self):
        try:
            data = self.runCommand('sensors -u')
        except:
            data = ''

        output = []
        output.append('')
        output.append('[sensors]')

        for line in data.split('\n'):
            if '_input' in line:
                output.append(line.strip())

        return '\n'.join(output)

    def runCommand(self,*args):

        cmd = ' '.join([str(x) for x in args])

        status,output = commands.getstatusoutput(cmd)

        self.log.info(cmd)

        if status!=0:
            self.log.error('Program running command')
            self.log.error('  cmd:    %s' % cmd)
            self.log.error('  status: %s' % status)
            self.log.error('  output: %s' % output)
            raise IOError('Problem running command')

        return output

if __name__ == '__main__':
    Server(sys.argv).run()

