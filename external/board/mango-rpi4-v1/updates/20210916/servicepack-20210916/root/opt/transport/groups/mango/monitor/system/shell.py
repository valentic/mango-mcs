#!/usr/bin/env python2

####################################################################
#
#   Data monitor shell
#
#   2013-03-04  Todd Valentic
#               Initial implementation
#
#   2019-06-27  Todd Valentic
#               Add TS7250status
#
#   2021-06-30  Todd Valentic
#               Add tincan
#               Remove TS7250status for mango systems
#
#   2021-09-17  Todd Valentic
#               Add restart monitor
#               Add log monitor
#
####################################################################

import sys

from Transport          import ProcessClient

from systemmonitor      import SystemMonitor
from schedulemonitor    import ScheduleMonitor
from watchdogmonitor    import WatchdogMonitor
from tincanmonitor      import TincanMonitor
from restartmonitor     import RestartMonitor
from logmonitor         import LogMonitor

class DataMonitorShell(ProcessClient):

    def __init__(self,argv):
        ProcessClient.__init__(self,argv)

        self.monitors = self.getComponents('monitors',self.monitorFactory)

    def monitorFactory(self,name,parent,**kw):

        id = self.get('monitor.%s.type'%name,self.get('monitor.*.type'))

        self.log.info('creating monitor: %s (%s)' % (id,name))

        map = {
            'system':   SystemMonitor,
            'schedule': ScheduleMonitor,
            'watchdog': WatchdogMonitor,
            'tincan':   TincanMonitor,
            'restart':  RestartMonitor,
            'log':      LogMonitor,
            }

        if id in map:
            return map[id](name,parent,**kw)
        else:
            raise ValueError('Unknown monitor: %s' % id)

    def run(self):

        steppers = [monitor.step() for monitor in self.monitors]

        while self.wait(60,sync=True):

            for stepper in steppers:
                try:
                    stepper.next()
                except StopIteration:
                    pass


if __name__ == '__main__':
    DataMonitorShell(sys.argv).run()

