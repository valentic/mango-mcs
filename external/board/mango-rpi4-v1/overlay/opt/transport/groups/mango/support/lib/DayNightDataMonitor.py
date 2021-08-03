#!/usr/bin/env python

###########################################################
#
#   Day/Night Data Monitor
#
#   A DataMonitor object with a schedule driven by the
#   location of the sun. We use the current GPS location
#   to compute solar local noon and operate within a
#   window around it.
#
#   2011-08-17  Todd Valentic
#               Initial implementation.
#               Based on SolarDataMonitor
#
#   2012-08-23  Todd Valentic
#               Differentiate between None and 0 in solar
#                   angle check.
#               Check for windowMax is None
#
#   2011-08-26  Todd Valentic
#               Invalidate nextSampleTime on schedule change
#
#   2012-03-04  Todd Valentic
#               Use DataMonitorMixin and create component
#
#   2021-04-11  Todd Valentic
#               Use DataMonitor cache methods
#
#   2021-06-30  Todd Valentic
#               Use location service
#
###########################################################

from DataMonitor import DataMonitorMixin
from Transport   import ProcessClient
from Transport   import ConfigComponent

import sys
import ephem

class DayNightDataMonitorMixin(DataMonitorMixin):

    def __init__(self):
        DataMonitorMixin.__init__(self)

        self.reportMissingGPS = True
        self.reportState = None

        self.location = self.connect('location')

    def whenOn(self):

        schedule = self.curSchedule

        # Swap schedules based on solar angle

        rate     = schedule.getDeltaTime('sample.rate',60)
        sync     = schedule.getboolean('sample.sync',True)
        offset   = schedule.getDeltaTime('sample.offset',0)

        self.log.debug('dumping schedule (%s)' % schedule.name)
        for option in schedule.options():
            self.log.debug('  %s: %s' % (option,schedule.get(option)))

        if self.sunUp():

            rate    = schedule.getDeltaTime('day.sample.rate',rate)
            sync    = schedule.getboolean('day.sample.sync',sync)
            offset  = schedule.getDeltaTime('day.sample.offset',offset)
            state   = 'day'

        else:
            rate    = schedule.getDeltaTime('night.sample.rate',rate)
            sync    = schedule.getboolean('night.sample.sync',sync)
            offset  = schedule.getDeltaTime('night.sample.offset',offset)
            state   = 'night'

        self.curSchedule.sampleRate = rate
        self.curSchedule.sampleSync = sync
        self.curSchedule.sampleOffset = offset

        if self.reportState != state:
            self.log.info('State change: %s -> %s' % (self.reportState,state))
            if self.reportState:
                self.nextSampleTime = None
            self.reportState = state

        DataMonitorMixin.whenOn(self)

    def inTimeSpan(self,now,transit,window,repeatDays):

        now = now.datetime()

        transitTime = transit.datetime()
        dayOfYear = int(transitTime.strftime('%j'))

        if repeatDays and dayOfYear%repeatDays!=0:
            return False

        return transitTime-window/2<=now and now<transitTime+window/2

    def sunUp(self):

        location = self.location.best()

        if not location:
            if self.reportMissingGPS:
                self.reportMissingGPS=False
                self.log.info('No GPS or Iridium position data...')
            return False

        self.reportMissingGPS=True

        now = ephem.now()

        station = ephem.Observer()
        station.lat = str(location['latitude'])
        station.long = str(location['longitude'])
        station.date = now

        self.log.debug('station: %s' % station)

        sun = ephem.Sun()
        sun.compute(station)
        solarAngle = float(sun.alt)*180/ephem.pi

        station.date = now
        nextTransit = station.next_transit(sun)

        station.date = now
        prevTransit = station.previous_transit(sun)

        self.log.debug('  prev transit: %s' % prevTransit)
        self.log.debug('  next transit: %s' % nextTransit)
        self.log.debug('  sun angle: %s' % solarAngle)

        schedule = self.curSchedule

        self.log.debug('  checking if in prev min')
        if self.inTimeSpan(now,prevTransit,schedule.windowMin,schedule.repeatDays):
            self.log.debug('    yes - turn on')
            return True

        self.log.debug('  checking if in next min')
        if self.inTimeSpan(now,nextTransit,schedule.windowMin,schedule.repeatDays):
            self.log.debug('    yes - turn on')
            return True

        if schedule.minSolarAngle is not None:
            self.log.debug('  checking min solar angle')
            if solarAngle < schedule.minSolarAngle:
                self.log.debug('    sun too low - turn off')
                return False

        if schedule.maxSolarAngle is not None:
            if solarAngle >= schedule.maxSolarAngle:
                self.log.debug('    sun too high - turn off')
                return False

        if schedule.windowMax is None:
            return True

        self.log.debug('  checking if in prev window')
        if self.inTimeSpan(now,prevTransit,schedule.windowMax,schedule.repeatDays):
            self.log.debug('    yes - turn on')
            return True

        self.log.debug('  checking if in next window')
        if self.inTimeSpan(now,nextTransit,schedule.windowMax,schedule.repeatDays):
            self.log.debug('    yes - turn on')
            return True

        self.log.debug('  failed to match any criteria')

        return False

class DayNightDataMonitorComponent(ConfigComponent,DayNightDataMonitorMixin):

    def __init__(self,*pos,**kw):
        ConfigComponent.__init__(self,'monitor',*pos,**kw)
        DayNightDataMonitorMixin.__init__(self)

        self.currentTime = self.parent.currentTime

class DayNightDataMonitor(ProcessClient,DayNightDataMonitorMixin):

    def __init__(self,argv):
        ProcessClient.__init__(self,argv)
        DayNightDataMonitorMixin.__init__(self)

        self.scheduleRate = self.getDeltaTime('schedule.rate',60)

    def run(self):

        for step in self.step():
            if not self.wait(self.scheduleRate,sync=True):
                break


if __name__ == '__main__':
    DayNightDataMonitor(sys.argv).run()


