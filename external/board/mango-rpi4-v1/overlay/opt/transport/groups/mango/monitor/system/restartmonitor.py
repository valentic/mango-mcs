#!/usr/bin/env python2

############################################################################
#
#   restart 
#
#   This script restarts the system by setting the restart.system flag at 
#   at scheduled time. The scheduler is solar driven so that we only
#   restart the system during day at solar local noon, when we are idle.
#
#   2021-09-17  Todd Valentic
#               Initial implementation
#
############################################################################

from SolarDataMonitor import SolarDataMonitorComponent

import  os
import  sys

class RestartMonitor(SolarDataMonitorComponent):

    def __init__(self,*pos,**kw):
        SolarDataMonitorComponent.__init__(self,*pos,**kw)

        self.server = self.parent.server
        self.flagfile = self.get('flagfile','restart')

    def sample(self):
        
        self.log.info('Writing restart flag: %s' % self.flagfile)
        
        open(self.flagfile,'w').write('restart')

