#!/usr/bin/env python

###########################################################
#
#   Solar Data Monitor
#
#   A DataMonitor object with a schedule driven by the
#   location of the sun. We use the current GPS location
#   to compute solar local noon and operate within a
#   window around it. We also have the ability to only
#   run on certain days. 
#
#   2009-11-05  Todd Valentic
#               Initial implementation
#
#   2009-11-09  Todd Valentic
#               Change some log output from info -> debug
#
#   2010-01-22  Todd Valentic
#               Incorporate loadable schedules.
#
#   2010-03-16  Todd Valentic
#               Fix solar angle check
#               Schedule framework moved to DataMonitor
#
#   2010-10-25  Todd Valentic
#               Repeat days should check transitTime
#
#   2011-08-25  Todd Valentic
#               Handle solar angle and windowMax = None
#               Use iridium position data if no GPS data
#
#   2013-03-04  Todd Valentic
#               Break into Mixin and Component classes
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

class SolarDataMonitorMixin(DataMonitorMixin):

    def __init__(self):
        DataMonitorMixin.__init__(self)

        self.location = self.connect('location')
        self.reportMissingGPS = True

    def inTimeSpan(self,now,transit,window,repeatDays):

        now = now.datetime()

        transitTime = transit.datetime()
        dayOfYear = int(transitTime.strftime('%j'))

        if repeatDays and dayOfYear%repeatDays!=0:
            return False

        return transitTime-window/2<=now and now<transitTime+window/2

    def checkWindow(self):

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
            self.log.debug('  checking solar angle')
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

class SolarDataMonitorComponent(ConfigComponent,SolarDataMonitorMixin):

    def __init__(self,*pos,**kw):
        ConfigComponent.__init__(self,'monitor',*pos,**kw)
        SolarDataMonitorMixin.__init__(self)

        self.currentTime = self.parent.currentTime

class SolarDataMonitor(ProcessClient,SolarDataMonitorMixin):

    def __init__(self,argv):
        ProcessClient.__init__(self,argv)
        SolarDataMonitorMixin.__init__(self)

        self.scheduleRate = self.getDeltaTime('schedule.rate',60)

    def run(self):

        for step in self.step():
            if not self.wait(self.scheduleRate,sync=True):
                break


if __name__ == '__main__':
    SolarDataMonitor(sys.argv).run()

