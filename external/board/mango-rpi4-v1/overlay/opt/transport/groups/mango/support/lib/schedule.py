#!/usr/bin/env python

#####################################################################
#
#   Schedule Manager
#
#   2010-01-21  Todd Valentic
#               Initial implementation
#
#   2010-03-17  Todd Valentic
#               Return None instead of exception if no
#                   matching schedule is found.
#
#               Add window start/stop/span and check
#
#   2010-03-18  Todd Valentic
#               Rework sample window to have rate/offset/span
#               Read schedule files one at a time in case one
#                   of them has errors (otherwise all were rejected).
#
#   2010-03-22  Todd Valentic
#               Make sure empty schedule list exists.
#
#   2010-04-29  Todd Valentic
#               Add enabled parameter
#               Make time comparisons in UTC
#
#   2011-04-02  Todd Valentic
#               Make schedule-specific config parameters available
#               Move getTimeSpan in to ExtendedConfigParser
#
#   2011-08-25  Todd Valentic
#               Add try..except around Schedule creation.
#               Default windowMax to None
#
#####################################################################

from ExtendedConfigParser import ExtendedConfigParser
from Transport.Util.dateutil import parser, tz
from Transport.Util.dateutil.relativedelta import relativedelta
from Transport.Util import datefunc

import datetime
import time
import os
import glob

import logging

utc = tz.tzutc()

def WrapFunction(func,section):
    def wrapper(*pos,**args):
        return func(section,*pos,**args)
    return wrapper

class Schedule:

    def __init__(self,name,config):

        self.name = name

        # Add get* method names here that call config.getX(name,...)
        # This lets us lookup schedule specific config values

        for method in dir(config):
            if method.startswith('get'):
                func = getattr(config,method)
                setattr(self,method,WrapFunction(func,name))

        self.options = WrapFunction(config.options,name)

        self.enabled        = self.getboolean('enabled',True)
        self.startTime      = self.get('time.start')
        self.stopTime       = self.get('time.stop')
        self.timeSpan       = self.getTimeSpan('time.span')
        self.minSolarAngle  = self.getfloat('solarangle.min',-6)
        self.maxSolarAngle  = self.getfloat('solarangle.max')
        self.repeatDays     = self.getint('repeat.days')
        self.priority       = self.getint('priority',10)
        self.windowMin      = self.getDeltaTime('window.min',0)
        self.windowMax      = self.getDeltaTime('window.max')
        self.sampleRate     = self.getDeltaTime('sample.rate',60)
        self.sampleSync     = self.getboolean('sample.sync',True)
        self.sampleOffset   = self.getDeltaTime('sample.offset',0)
        self.sampleAtStart  = self.getboolean('sample.atstart',False)

        self.windowRate     = self.getDeltaTime('window.rate')
        self.windowOffset   = self.getDeltaTime('window.offset',0)
        self.windowSpan     = self.getDeltaTime('window.span',60)

        if self.windowRate:
            self.windowRate = datefunc.timedelta_as_seconds(self.windowRate)
            self.windowOffset = datefunc.timedelta_as_seconds(self.windowOffset)
            self.windowSpan = datefunc.timedelta_as_seconds(self.windowSpan)

    def match(self,targetTime):

        now = datetime.datetime.now()
        thisYear = datetime.datetime(now.year,1,1)

        try:
            startTime = parser.parse(self.startTime,default=thisYear)
            startTime = startTime.replace(tzinfo=utc)
        except:
            startTime = None

        try:
            stopTime = parser.parse(self.stopTime,default=thisYear)
            stopTime = stopTime.replace(tzinfo=utc)
        except:
            stopTime = None

        if startTime and not stopTime and self.timeSpan:
            stopTime = startTime + self.timeSpan

        if startTime and stopTime:
            if stopTime < startTime:
                stopTime += relativedelta(years=1)

        if startTime and targetTime<startTime:
            return False

        if stopTime and targetTime>=stopTime:
            return False

        return True

    def inWindow(self):

        if not self.windowRate:
            return True

        now = time.time()

        startTime = int(now/self.windowRate)*self.windowRate+self.windowOffset
        stopTime  = startTime + self.windowSpan

        return now>=startTime and now<stopTime


class ScheduleManager:

    def __init__(self,log):

        self.log = log
        self.filetimes = {}
        self.schedules = []

    def load(self,filenames):

        self.log.info('Loading schedules:')

        self.schedules = []

        config = ExtendedConfigParser()

        for filename in sorted(filenames):
            try:
                config.read(filename)
            except:
                self.log.exception('Failed to read %s' % filename)

        for section in config.sections():
            try:
                schedule = Schedule(section,config)
                self.schedules.append((schedule.priority,schedule))
            except:
                self.log.error('Failed to create schedule %s' % section)

        self.schedules.sort(reverse=True)

        for priority,schedule in self.schedules:
            self.log.info('  - [%d] %s' % (schedule.priority,schedule.name))

    def reload(self,filespecs):

        filenames = []

        for filespec in filespecs:
            filenames.extend(glob.glob(filespec))

        reload = False

        newfiles    = set(filenames).difference(self.filetimes)
        stalefiles  = set(self.filetimes).difference(filenames)
        checkfiles  = set(self.filetimes).intersection(filenames)

        if newfiles or stalefiles:
            reload = True

        curtimes = {}

        for filename in newfiles:
            curtimes[filename] = os.stat(filename).st_mtime

        for filename in checkfiles:
            mtime = os.stat(filename).st_mtime
            if mtime != self.filetimes[filename]:
                reload = True
            curtimes[filename]=mtime

        self.filetimes = curtimes

        if reload:
            self.load(filenames)

        return reload

    def match(self,time):

        for priority,schedule in self.schedules:
            if schedule.match(time):
                return schedule

        return None


if __name__ == '__main__':

    import logging
    logging.basicConfig(level=logging.INFO)

    scheduler = ScheduleManager(logging)

    filespec = ['demo.conf','year.conf']

    while True:

        time.sleep(1)

        if scheduler.reload(filespec):
            print '='*70
            print '='*70

        now = datetime.datetime.now(utc)

        schedule = scheduler.match(now)

        print '-'*60
        print 'Current schedule'

        for key in sorted(vars(schedule)):
            if not key.startswith('_'):
                print '%20s: %s' % (key,getattr(schedule,key))

        print '  special: pump=',schedule.getint('pump')

