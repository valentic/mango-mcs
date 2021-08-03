#!/usr/bin/env python2

##################################################################
#
#   Monitor for schedule changes
#
#   2011-05-16  Todd Valentic
#               Initial implementation.
#
#   2013-03-04  Todd Valentic
#               Migrated to use DataMonitorComponent
#
##################################################################

from DataMonitor import DataMonitorComponent

import tarfile
import StringIO
import glob
import os
import md5

class ScheduleMonitor (DataMonitorComponent):

    def __init__(self,*pos,**kw):
        DataMonitorComponent.__init__(self,*pos,**kw)

        self.schedulePattern = os.path.join(self.get('path.schedules'),'*.conf')

        self.loadChecksum()

    def loadChecksum(self):

        try:
            self.checksum = open('checksum').read()
        except:
            self.checksum = None

    def saveChecksum(self,checksum):

        self.checksum = checksum
        open('checksum','w').write(checksum)

    def sample(self):

        buffer = StringIO.StringIO()
        tarball = tarfile.open(fileobj=buffer,mode='w')

        for filename in glob.glob(self.schedulePattern):
            tarball.add(filename)
        tarball.close()

        output = buffer.getvalue()
        checksum = md5.md5(output).hexdigest()

        if checksum!=self.checksum:
            self.log.info('Detected modified schedules')
            self.saveChecksum(checksum)
            return output
        else:
            return None


