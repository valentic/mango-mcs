#!/usr/bin/env python2

############################################################################
#
#   watchdog
#
#   This script monitors the process group clients and reports any that
#   were started but have seemed to stop running (they are registered but
#   don't show up as an active process). In this case, the client is
#   restarted. Based on the standard watchdog code.
#
#   2009-11-12  Todd Valentic
#               Initial implementation
#
#   2013-03-04  Todd Valentic
#               Migrate to DataMonitorComponent framework
#
############################################################################

from DataMonitor import DataMonitorComponent

import  os
import  sys

class WatchdogMonitor(DataMonitorComponent):

    def __init__(self,*pos,**kw):
        DataMonitorComponent.__init__(self,*pos,**kw)

        self.server = self.parent.server

    def getClientPids(self):

        pids = {}

        for group in self.server.listgroups():
            for client,info in self.server.listclients(group).items():
                pid = info[1]
                if pid:
                    pids[pid] = (group,client)

        return pids

    def getRunningPids(self):

        files = os.listdir('/proc')

        pids = []

        for file in os.listdir('/proc'):
            try:
                pids.append(int(file))
            except:
                pass

        return pids

    def restart(self,pid,group,client):

        self.log.info('Process %d (%s %s) is missing' % (pid,group,client))
        self.log.info('  Attempting to restart')

        try:
            self.server.startclient(group,client)
        except:
            self.log.info('  Error detected in client restart!')

    def sample(self):

        self.log.debug('Checking processes')

        try:
            self.server.status()
        except:
            self.log.error('Cannot connect to the transport server!')
            return

        clientPids  = self.getClientPids()
        runningPids = self.getRunningPids()

        for pid in set(clientPids).difference(runningPids):
            group,client = clientPids[pid]
            self.restart(pid,group,client)

