#!/usr/bin/env python2

##################################################################
#
#   Save log files  
#
#   2021-09-21  Todd Valentic
#               Initial implementation.
#
##################################################################

from DataMonitor import DataMonitorComponent

import tarfile
import StringIO

class LogMonitor (DataMonitorComponent):

    def __init__(self,*pos,**kw):
        DataMonitorComponent.__init__(self,*pos,**kw)

        self.logPathnames = self.getList('path.logfiles')

    def sample(self):

        buffer = StringIO.StringIO()
        tarball = tarfile.open(fileobj=buffer,mode='w')

        for pathname in self.logPathnames: 
            tarball.add(pathname)
        tarball.close()

        return buffer.getvalue()


