#!/usr/bin/env python

###########################################################
#
#   Power Data Monitor
#
#   A DataMonitor object with a schedule driven by the
#   current power level. We use the charge power input
#   measured by the SunSaver charge controller.
#
#   2016-08-11  Todd Valentic
#               Initial implementation.
#               Based on PowerDataMonitor
#
###########################################################

from DataMonitor import DataMonitorMixin
from Transport   import ProcessClient
from Transport   import ConfigComponent

import sys
import sunsaver

class PowerDataMonitorMixin(DataMonitorMixin):

    def __init__(self):
        DataMonitorMixin.__init__(self)

        self.cache = self.connect('cache')
        self.reportMissingGPS = True
        self.reportState = None

    def whenOn(self):

        schedule = self.curSchedule

        # Swap schedules based on solar angle

        rate     = schedule.getDeltaTime('sample.rate',60)
        sync     = schedule.getboolean('sample.sync',True)
        offset   = schedule.getDeltaTime('sample.offset',0)

        self.log.debug('dumping schedule (%s)' % schedule.name)
        for option in schedule.options():
            self.log.debug('  %s: %s' % (option,schedule.get(option)))

        if self.powerGood():

            rate    = schedule.getDeltaTime('power.good.sample.rate',rate)
            sync    = schedule.getboolean('power.good.sample.sync',sync)
            offset  = schedule.getDeltaTime('power.good.sample.offset',offset)
            state   = 'power.good'

        else:
            rate    = schedule.getDeltaTime('power.low.sample.rate',rate)
            sync    = schedule.getboolean('power.low.sample.sync',sync)
            offset  = schedule.getDeltaTime('power.low.sample.offset',offset)
            state   = 'power.low'

        self.curSchedule.sampleRate = rate
        self.curSchedule.sampleSync = sync
        self.curSchedule.sampleOffset = offset

        if self.reportState != state:
            self.log.info('State change: %s -> %s' % (self.reportState,state))
            if self.reportState:
                self.nextSampleTime = None
            self.reportState = state

        DataMonitorMixin.whenOn(self)

    def getPowerWatts(self):
        try:
            rawdata = self.cache.get('sunsaver')
            data = sunsaver.Parse(rawdata)
        except:
            return 0

        return data['power_out']

    def powerGood(self):

        powerWatts = self.getPowerWatts()

        schedule = self.curSchedule

        minPowerWatts = schedule.getfloat('power.good.watts',20)

        if powerWatts>=minPowerWatts:
            self.log.debug('    power good %.1f >= %.1f' % (powerWatts,minPowerWatts))
            return True
        else:
            self.log.debug('    power low %.1f < %.1f' % (powerWatts,minPowerWatts))
            return False

        return False

class PowerDataMonitorComponent(ConfigComponent,PowerDataMonitorMixin):

    def __init__(self,*pos,**kw):
        ConfigComponent.__init__(self,'monitor',*pos,**kw)
        PowerDataMonitorMixin.__init__(self)

        self.currentTime = self.parent.currentTime

class PowerDataMonitor(ProcessClient,PowerDataMonitorMixin):

    def __init__(self,argv):
        ProcessClient.__init__(self,argv)
        PowerDataMonitorMixin.__init__(self)

        self.scheduleRate = self.getDeltaTime('schedule.rate',60)

    def run(self):

        for step in self.step():
            if not self.wait(self.scheduleRate,sync=True):
                break


if __name__ == '__main__':
    PowerDataMonitor(sys.argv).run()


