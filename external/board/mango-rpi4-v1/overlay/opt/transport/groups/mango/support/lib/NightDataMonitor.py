#!/usr/bin/env python

#####################################################################
#
#   Night Data Monitor
#
#   A DataMonitor object with a schedule driven by the
#   location of the sun. We use the current GPS location
#   to compute solar elevation angle and turn on when 
#   the sun is below a given angle.
#
#   2021-06-10  Todd Valentic
#               Initial implementation. Based on SolarDataMonitor
#
#   2021-06-30  Todd Valentic
#               Use location service
#
#   2021-07-23  Todd Valentic
#               Base window selection on rising/setting times
#               Add warmup time parameter
#
#####################################################################

from DataMonitor import DataMonitorMixin
from Transport   import ProcessClient
from Transport   import ConfigComponent

import sys
import pytz
import ephem

class NightDataMonitorMixin(DataMonitorMixin):

    def __init__(self):
        DataMonitorMixin.__init__(self)

        self.reportMissingLocation = True
        self.location = self.connect('location')

    def checkWindow(self):

        schedule = self.curSchedule
        location = self.location.best()

        if not location: 
            # Report missing data once so we don't spam the log 
            if self.reportMissingLocation:
                self.reportMissingLocation=False
                self.log.info('No location data.')
            return False
        else:
            # We got a good position, reset flag
            self.reportMissingLocation=True

        if schedule.minSolarAngle is None:
            # Nothing to do
            return False

        offset_time = schedule.getDeltaTime('start.offset')

        sun = ephem.Sun()

        station = ephem.Observer()
        station.lat = str(location['latitude'])
        station.lon = str(location['longitude'])
        station.date = ephem.now() 
        station.horizon = str(schedule.minSolarAngle)

        self.log.debug('Station: %s' % station)

        # If the sun is already set, then turn on

        sun.compute(station)
        solarAngle = float(sun.alt)*180/ephem.pi

        self.log.debug('Checking solar angle: %s' % solarAngle)

        if solarAngle <= schedule.minSolarAngle:
            self.log.debug('  - sun is below min elevation. Turn on')
            return True 

        # If we are approaching sunset, check the offset time

        if offset_time is not None:

            self.log.debug('Checking sunset time with offset %s' % offset_time)

            try:
                next_setting = station.next_setting(sun, use_center=True)
                self.log.debug('  next setting at %s' % next_setting)
            except ephem.AlwaysUpError:
                self.log.debug('  sun is always up. Turn off.')
                return False
            except ephem.NeverUpError:
                self.log.debug('  sun is never up. Turn on.')
                return True

            next_setting = next_setting.datetime().replace(tzinfo=pytz.utc)

            if self.currentTime() >= (next_setting - offset_time):
                self.log.debug('  in pre-sunset time window. Turn on') 
                return True

        self.log.debug('Sun is above min elevation - turn off')

        return False

class NightDataMonitorComponent(ConfigComponent,NightDataMonitorMixin):

    def __init__(self,*pos,**kw):
        ConfigComponent.__init__(self,'monitor',*pos,**kw)
        NightDataMonitorMixin.__init__(self)

        self.currentTime = self.parent.currentTime

class NightDataMonitor(ProcessClient,NightDataMonitorMixin):

    def __init__(self,argv):
        ProcessClient.__init__(self,argv)
        NightDataMonitorMixin.__init__(self)

        self.scheduleRate = self.getDeltaTime('schedule.rate',60)

    def run(self):

        for step in self.step():
            if not self.wait(self.scheduleRate,sync=True):
                break


if __name__ == '__main__':
    NightDataMonitor(sys.argv).run()

