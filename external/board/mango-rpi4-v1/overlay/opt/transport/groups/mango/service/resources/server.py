#!/usr/bin/env python2

###########################################################
#
#   Resource Monitor Service
#
#   Manage onboard resources
#
#   2009-08-21  Todd Valentic
#               Initial implementation.
#
#   2009-10-21  Todd Valentic
#               First release version.
#
#   2010-01-25  Todd Valentic
#               Add background states and delay
#
#   2010-03-16  Todd Valentic
#               Remove need to explicitly list users
#
#   2021-07-12  Todd Valentic
#               Run reconcile in background
#
###########################################################

from Transport      import ProcessClient
from Transport      import XMLRPCServerMixin
from Transport      import AccessMixin
from Transport      import ConfigComponent
from Transport.Util import PatternTemplate
from Transport.Util import datefunc

import sys
import ConfigParser
import StringIO
import commands
import signal

class ResourceState(ConfigComponent):

    def __init__(self,name,parent):
        ConfigComponent.__init__(self,'state',name,parent)

        replaceState = PatternTemplate('state')
        self.command = self.get('command')
        self.service = self.get('service')
        self.values  = self.getList('values',self.get('value',''))

        if self.command:
            self.command = replaceState(self.command,name)

        if self.service:
            self.service = replaceState(self.service,name)

class Resource(ConfigComponent):

    def __init__(self,name,parent):
        ConfigComponent.__init__(self,'resource',name,parent)

        self.order      = self.getList('states')
        self.states     = self.getComponentsDict('states',ResourceState)
        self.params     = set(self.getList('params'))
        self.reset      = self.get('reset.state',self.order[0])
        self.default    = self.get('default.state',self.order[-1])
        self.section    = self.get('status.section','DEFAULT')
        self.key        = self.get('status.key','')

        if not self.params:
            self.params.add('')

        self.replaceParam = PatternTemplate('param')

        if self.reset not in self.order:
            ValueError('Unknown reset.state (%s) for %s' % (self.reset,name))

        if self.default not in self.order:
            ValueError('Unknown default.state (%s) for %s' % (self.reset,name))

        self.log.info('Resource %s:' % name)

        for id in self.order:
            state = self.states[id]
            if state.command:
                self.log.info('  %s: [C] %s' % (state.name,state.command))
            if state.service:
                self.log.info('  %s: [S] %s' % (state.name,state.service))


    def getStatus(self,config,param):

        key = self.replaceParam(self.key,param)
        value = config.get(self.section,key)

        for state in self.states.values():
            if value in state.values:
                return state.name

        raise ValueError('Unknown status for %s (%s)' % (self.name,value))

    def resetState(self):
        return (self.params,self.reset)

    def rollup(self,entries):

        paramsdict = {}

        for param in self.params:
            paramsdict[param] = set()

        for entryparams,state in entries.values():
            for param in entryparams:
                paramsdict[param].add(state)

        result = {}

        for param,states in paramsdict.items():
            result[param] = self.selectState(states)

        return result

    def selectState(self,states):

        for state in reversed(self.order):
            if state in states:
                return state

        return self.default

    def reconcile(self,config,entry):

        for param,nextState in entry.items():

            try:
                curState = self.getStatus(config,param)
            except:
                self.log.exception('Failed to get status for %s' % self.name)
                continue

            if curState!=nextState:
                self.log.info('  state change %s[%s] %s -> %s' % \
                    (self.name,param,curState,nextState))
                self.runCommand(self.states[nextState].command,param)
                self.runService(self.states[nextState].service,param)

    def runCommand(self,cmd,param):

        if not cmd:
            return

        cmd = self.replaceParam(cmd,param)
        status,output = commands.getstatusoutput(cmd)

    def runService(self,cmd,param):

        if not cmd:
            return

        cmd = self.replaceParam(cmd,param).split()
        serviceName,function,args = cmd[0],cmd[1],cmd[2:]

        service = self.parent.connect(serviceName)
        getattr(service,function)(*args)

class ScoreBoard(AccessMixin):

    def __init__(self,parent,resources):
        AccessMixin.__init__(self,parent)

        self.users      = set()
        self.resources  = resources

        self.reset()

    def reset(self):

        self.map = {}

        for resource in self.resources.values():
            self.map[resource.name] = {}

    def allocate(self,user,resourceList):

        if user not in self.users:
            for resource in self.resources.values():
                self.map[resource.name][user] = resource.resetState()
                self.users.add(user)

        requested = {}
        for entry in resourceList:

            entry = entry.strip()

            if '=' in entry:
                resource,state = entry.rsplit('=',1)
            else:
                resource,state = entry,None

            if '[' in resource:
                resource,params = resource.split('[',1)
                params = params.replace(']','')
                params = set([p.strip() for p in params.split(',')])
            else:
                params = set([''])

            if resource not in self.resources:
                self.log.error('Unknown resource request: %s' % entry)
                continue

            requested[resource] = (params,state)

        for resource in set(self.resources).difference(requested):
            self.map[resource][user] = self.resources[resource].resetState()

        for resource in requested:
            self.map[resource][user] = requested[resource]

    def rollup(self):

        nextState = {}

        for resource,users in self.map.items():
            nextState[resource] = self.resources[resource].rollup(users)

        return nextState

class ResourceMonitor(ProcessClient, XMLRPCServerMixin):

    def __init__(self,argv):
        ProcessClient.__init__(self,argv)
        XMLRPCServerMixin.__init__(self,callback=self.reconcile,timeout=10)

        self.register_function(self.status)
        self.register_function(self.allocate)

        self.resources      = self.getComponentsDict('resources',Resource)
        self.statusCommand  = self.get('status.command')
        self.statusService  = self.get('status.service')

        self.scoreboard = ScoreBoard(self,self.resources)

        self.allocate('background',self.getList('background.state.start'))

        delay = self.getDeltaTime('background.delay',30)
        delay = int(datefunc.timedelta_as_seconds(delay))

        signal.signal(signal.SIGALRM,self.alarmHandler)
        signal.alarm(delay)

    def alarmHandler(self,signum,frame):
        self.allocate('background',self.getList('background.state.main'))

    def status(self):
        return str(self.scoreboard.map)

    def allocate(self,monitor,resources):
        self.log.info('allocation request from %s' % monitor)
        self.log.info('  %s' % resources)
        try:
            self.scoreboard.allocate(monitor,resources)
        except:
            self.log.exception('Problem parsing resource request')
            raise

        self.reconcile()

        return True

    def reconcile(self):

        try:
            self._reconcile()
        except:
            self.log.exception('Problem reconciling system state')
            raise

    def _reconcile(self):
        self.log.debug('Reconciling system state')

        try:
            curState  = self.getCurrentState()
        except:
            self.log.exception('Failed to get current state')
            return

        nextState = self.scoreboard.rollup()

        for resource,users in nextState.items():
            self.resources[resource].reconcile(curState,users)

        self.log.debug('  finished')

    def getCurrentState(self):

        if self.statusCommand:
            output = self.runCommand(self.statusCommand)
        elif self.statusService:
            output = self.runService(self.statusService)
        else:
            raise IOError('No status service/command given')

        buffer = StringIO.StringIO(output)
        curState = ConfigParser.ConfigParser()
        curState.readfp(buffer)

        return curState

    def runCommand(self,cmd):
        status,output = commands.getstatusoutput(cmd)

        if status!=0:
            self.log.error('Failed to run status command')
            self.log.error('  command: %s' % cmd)
            self.log.error('  status:  %s' % status)
            self.log.error('  output:  %s' % output)
            raise IOError()

        return output

    def runService(self,cmd):

        cmd = cmd.split()
        serviceName,function,args = cmd[0],cmd[1],cmd[2:]

        service = self.connect(serviceName)
        return getattr(service,function)(*args)

    def run(self):
        XMLRPCServerMixin.run(self)
        self.allocate('background',self.getList('background.state.stop'))

if __name__ == '__main__':
    ResourceMonitor(sys.argv).run()

