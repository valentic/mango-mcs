#!/usr/bin/env python2

###################################################################
#
#   PDU control service
#
#   This service is used to access the power distribution unit (PDU)
#   The heavy lifting is done through the pductl program.
#
#   2018-05-30  Todd Valentic
#               Initial implementation.
#
#   2018-06-12  Todd Valentic
#               Expand to handle multiple PDUs
#
#   2018-07-16  Todd Valentic
#               Add stages
#
#   2019-07-02  Todd Valentic
#               Support new PDU models (small payload)
#
#   2021-09-22  Todd Valentic
#               Add support for command retries
#
###################################################################

from Transport  import ProcessClient
from Transport  import XMLRPCServerMixin
from Transport  import ConfigComponent

import sys
import commands
import ConfigParser
import StringIO

from pdulib import PDU

class Stage(ConfigComponent):

    def __init__(self,name,parent):
        key = 'pdu.%s.rail.%s.stage' % (parent.parent.name,parent.name)
        ConfigComponent.__init__(self,key,name,parent)

        self.lookupStack.insert(1,'pdu.*.rail.*.stage.*.')
        self.lookupStack.insert(2,'pdu.*.rail.*.stage.%s.' % name)
        self.lookupStack.insert(3,'pdu.*.rail.%s.stage.*.' % parent.name)
        self.lookupStack.insert(4,'pdu.*.rail.%s.stage.%s.' % (parent.name,name))
        self.lookupStack.insert(5,'pdu.%s.rail.*.stage.*.' % (parent.parent.name))
        self.lookupStack.insert(6,'pdu.%s.rail.*.stage.%s.' % (parent.parent.name,name))

        self.log.info('Stage lookup: %s' % self.lookupStack)

        self.duration = self.getint('duration',0)
        self.state    = self.get('state','on')

    def asDict(self):
        return dict(duration=self.duration,state=self.state,name=self.name)

class Rail(ConfigComponent):

    def __init__(self,name,parent):
        ConfigComponent.__init__(self,'pdu.%s.rail' % parent.name,name,parent)

        self.lookupStack.insert(1,'pdu.*.rail.*.')
        self.lookupStack.insert(3,'pdu.*.rail.%s.' % name)

        self.log.info('Rail lookup: %s' % self.lookupStack)

        self.stages = self.getComponentsList('stages',Stage)

        self.label      = self.get('label','')
        self.device     = self.get('device','')
        self.stopState  = self.get('stopState','off')
        self.enabled    = True

        if not self.device:
            if not self.label:
                self.enabled = False
            else:
                self.device = self.label.lower().replace(' ','_')

        if self.enabled:
            self.log.info('%s: %s' % (name,self.label))

    def asDict(self):

        return dict(
                    label       = self.label,
                    device      = self.device,
                    enabled     = self.enabled,
                    stopState   = self.stopState,
                    stages      = [stage.asDict() for stage in self.stages]
                    )

class PDUConfig(ConfigComponent):

    def __init__(self,name,parent):
        ConfigComponent.__init__(self,'pdu',name,parent)

        self.model      = self.get('model','v2')
        self.addr       = self.get('addr')
        self.auth       = self.get('auth')
        self.scheme     = self.get('scheme')
        self.maxRetries = self.getint('retry.max',3)
        self.retryWait  = self.getDeltaTime('retry.wait',2)
        self.rails      = self.getComponentsDict('rails',Rail)

        args = dict(
            model   = self.model,
            addr    = self.addr,
            auth    = self.auth,
            scheme  = self.scheme,
            )

        self.pdu = PDU(log=self.log,**args)

        self.log.info('PDU %s: [%s] %s' % (name,self.model,self.addr))

    def run(self,cmd,*args):

        self.log.debug('%s %s' % (cmd,args))

        retryCount = 0

        while retryCount <= self.maxRetries and self.running:

            with self.pdu as pdu:
                try:
                    func = getattr(pdu,cmd)
                    return func(*args)
                except:
                    self.log.exception('error for cmd %s' % cmd)
                    retryCount += 1

            self.wait(self.retryWait)

        raise IOError('Timeout running %s' % cmd)

    def asDict(self):

        rails = {}

        for key,rail in self.rails.items():
            rails[key] = rail.asDict()

        return dict(
                    rails   = rails,
                    model   = self.model,
                    addr    = self.addr,
                    )

    def getDevices(self):
        return [rail.device for rail in self.rails.values() if rail.enabled]

class Server(ProcessClient,XMLRPCServerMixin):

    def __init__(self,args):
        ProcessClient.__init__(self,args)
        XMLRPCServerMixin.__init__(self)

        self.pdus = self.getComponentsDict('pdus',PDUConfig)

        self.register_function(self.status)
        self.register_function(self.cmd)
        self.register_function(self.list)

        self.register_function(self.getDeviceStateAsConfig)
        self.register_function(self.getDeviceState)
        self.register_function(self.getDevices)
        self.register_function(self.setDevice)

        self.cache = self.connect('cache')

    def status(self,id,*pos,**kw):
        pdu = self.pdus[id]
        results = pdu.run('status')
        self.cache.put(pdu.name,results)
        return results

    def getDeviceStateAsConfig(self):
        info   = self.list()
        config = ConfigParser.SafeConfigParser()
        section = 'devices'

        config.add_section(section)

        for pdu in info:
            status = self.status(pdu)
            for rail,entry in info[pdu]['rails'].items():   
                state = status['RS'][int(rail)-1]
                device = entry['device']
                config.set(section,device,str(state))

        buffer = StringIO.StringIO()
        config.write(buffer)

        return buffer.getvalue()

    def getDevices(self):

        results = [] 

        for pdu in self.pdus.values():
            results.extend(pdu.getDevices())

        return results

    def setDevice(self,name,state):

        if state in ['on','1',1,True]:
            state='on'
        else:
            state='off'

        for pdu_name,pdu in self.pdus.items():
            for rail_name,rail in pdu.rails.items():
                if not rail.enabled:
                    continue

                if rail.device==name:    
                    self.cmd(pdu_name,'SP',rail_name,state)
                    self.log.info('%s %s' % (name,state))
                    return True

        return False

    def getDeviceState(self):

        results = {}

        for pdu_name,pdu in self.pdus.items(): 
            status = self.status(pdu_name)
            for rail_name,rail in pdu.rails.items(): 
                state = status['RS'][int(rail_name)-1]
                results[rail.device] = state

        return results 

    def getDeviceStateAsConfig(self):

        config = ConfigParser.SafeConfigParser()
        section = 'device'

        config.add_section(section)

        for device,state in self.getDeviceState().items(): 
            config.set(section,device,str(state))

        buffer = StringIO.StringIO()
        config.write(buffer)

        return buffer.getvalue()

    def cmd(self,id,*pos,**kw):
        pdu = self.pdus[id]
        self.log.info('Cmd: %s' % (' '.join(pos)))
        return pdu.run(*pos)

    def list(self):

        results = {}

        for key,pdu in self.pdus.items():
            results[key] = pdu.asDict()

        return results

if __name__ == '__main__':
    Server(sys.argv).run()

