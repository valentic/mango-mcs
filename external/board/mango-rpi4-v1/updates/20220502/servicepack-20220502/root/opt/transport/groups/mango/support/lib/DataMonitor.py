#!/usr/bin/env python

##################################################################
#
#   Base class for data monitoring applications.
#
#   States:
#
#                       ----------------
#                      /  S  S  S  S  S \
#      |---------------                  ------------|
#      1      2        3        4       5            6
#
#      1 - startup
#      2 - whenOff
#      3 - goingOffToOn
#      4 - whenOn
#      5 - goingOnToOff
#      6 - shutdown
#
#      S - sample
#
#   In state 4 (on), we periodically sample at S.
#
#
#   2009-09-01  Todd Valentic
#               Initial implementation. Based on Imnavait Creek.
#
#   2009-10-30  Todd Valentic
#               Add output.rate support.
#               Renamed saveBinary -> saveData
#
#   2009-11-04  Todd Valentic
#               Add sample.offset
#               Add startup() and shutdown()
#               Add resource management
#
#   2009-11-05  Todd Valentic
#               Add scheduler and states
#
#   2009-11-06  Todd Valentic
#               Allow multiple resources in add/free
#
#   2009-11-11  Todd Valentic
#               Add setResources/clearResources. Clients should
#                   use these instead of directly calling allocate()
#
#   2009-11-18  Todd Valentic
#               Fix allocation bug in default shutdown()
#
#   2010-01-22  Todd Valentic
#               Add runScript() to consolidate script exec
#               Add getStatus()
#
#   2010-03-17  Todd Valentic
#               Split off resource management into ResourceMixin
#               Add reloadable schedules
#
#   2010-04-29  Todd Valentic
#               Add schedule enabled check
#
#   2013-03-04  Todd Valentic
#               Split DataMonitor core into mixin
#
#   2013-05-14  Todd Valentic
#               Make sure to only grab the current instrument
#                   files in compressFiles()
#
#   2017-10-16  Todd Valentic
#               Add compression parameter - sometimes we don't
#                   want to compress small binary files.
#
#   2019-04-24  Todd Valentic
#               Add output file flush and fsync when writing
#                   to make writes more robust.
#
#   2019-04-29  Todd Valentic
#               Make status service configurable 
#
#   2019-06-10  Todd Valentic
#               Include robust cache connection. A lot of derived 
#                   clients use the cache, which at one point 
#                   stopped and became a single point of failure.
#                   We include try..except protection and report
#                   errors here.
#
#   2019-07-22  Todd Valentic
#               Add outputenabled (default True)
#               Check if output path needs to be created
#
#   2021-04-07  Todd Valentic
#               Typo calling statusMethod
#               Change test for valid data from truth to None   
#               Add listCache()
#
#   2021-07-14  Todd Valentic
#               Use time at sample start for file timestamp
#
#   2022-05-02  Todd Valentic
#               Add sampleTime get/set 
#
##################################################################

from Transport import ProcessClient
from Transport import DirectoryMixin
from Transport import ConfigComponent
from Transport.Util import datefunc

from subprocess import Popen,PIPE

import os
import stat
import time
import struct
import glob
import bz2
import sys
import StringIO
import ConfigParser
import datetime

import schedule
from ResourceMixin import ResourceMixin

class DataMonitorMixin(DirectoryMixin,ResourceMixin):

    def __init__(self):
        DirectoryMixin.__init__(self)
        ResourceMixin.__init__(self)

        statusServiceName   = self.get('status.service','sbcctl') 
        self.statusMethod   = self.get('status.method','status') 

        self.statusService  = self.connect(statusServiceName)
        self.cacheService   = self.connect('cache')
        self.schedules      = schedule.ScheduleManager(self.log)
        self.curSchedule    = None
        self.on             = False
        self._sampleTime    = None

        self.outputenabled  = self.getboolean('output.enabled',True)
        self.outputrate	    = self.getDeltaTime('output.rate')
        self.outputpath     = self.get('output.path','')
        self.outputname     = self.get('output.name','data-%Y%m%d-%H%M%S.dat')
        self.compress       = self.getboolean('output.compress',True)
        self.scriptPath     = self.get('scripts')
        self.scheduleRate   = self.getDeltaTime('schedule.rate',60)
        self.scheduleFiles  = self.getList('schedule.files')
        self.windowFlag     = self.get('force.flag.window')
        self.sampleFlag     = self.get('force.flag.sample')

        if self.outputrate:
            self.interval = datefunc.timedelta_as_seconds(self.outputrate)

        self.schedules.reload(self.scheduleFiles)

    def getStatus(self,*pos,**kw):

        status = ConfigParser.ConfigParser()

        try:
            func = getattr(self.statusService,self.statusMethod)
            buffer = StringIO.StringIO(func(*pos,**kw))
            status.readfp(buffer)
        except:
            self.log.exception('Failed to get status')

        return status

    def putCache(self,key,value):
        try:    
            return self.cacheService.put(key,value)
        except:
            self.log.exception('Failed to set cache')
            return False

    def getCache(self,key):

        try:
            # server not configured for None yet
            #return self.cacheService.get_or_default(key,None)
            return self.cacheService.get(key)
        except:
            self.log.error('No cache value for %s' % key)
            return None

    def listCache(self):
        try:
            return self.cacheService.list()
        except:
            return []

    def runScript(self,basename,timeout=None):
        script = os.path.join(self.scriptPath,basename)

        if timeout:
            script = '/usr/bin/timeout -t %s %s' % (timeout,script)

        self.log.info('Running %s' % script)
        process = Popen(script.split(),stdout=PIPE,stderr=PIPE,cwd=self.scriptPath)
        self.subprocs.add(process)
        response = process.communicate()
        self.subprocs.remove(process)

        if process.returncode:
            self.log.error('Problem running script')
            self.log.error('   script: %s' % script)
            self.log.error('   status: %s' % process.returncode)
            self.log.error('   stdout: %s' % response[0])
            self.log.error('   stderr: %s' % response[1])

            return False

        return True

    def getInterval(self,timestamp):
        if self.outputrate:
            return int(timestamp/self.interval)*self.interval
        else:
            return timestamp

    def sample(self):
        # Filled in by child class, returns data to be written
        return None

    def write(self,output,timestamp,data):
        # Usually supplied by child class. Default is to just write.
        output.write(data)

    def startup(self):
        # Filled in by child class
        pass

    def shutdown(self):
        # Filled in by child class - default free all resources
        self.clearResources()

    def whenOff(self):
        # Filled in by child class
        pass

    def goingOffToOn(self):
        # Filled in by child class
        self.on=True

    def computeNextSampleTime(self):

        if not self.curSchedule:
            return None

        rate   = datefunc.timedelta_as_seconds(self.curSchedule.sampleRate)
        offset = datefunc.timedelta_as_seconds(self.curSchedule.sampleOffset)

        if self.curSchedule.sampleSync:
            curtime = time.time()-offset
            waittime = rate-curtime%rate
        else:
            waittime = rate

        return self.currentTime() + datetime.timedelta(seconds=waittime)

    def whenOn(self):

        if not self.nextSampleTime:
            self.nextSampleTime = self.computeNextSampleTime()

        if self.checkFlag(self.sampleFlag):
            self.nextSampleTime = self.currentTime()
            try:
                os.remove(self.sampleFlag)
            except:
                self.log.exception('Failed to remove sample flag')

        self.log.debug('Next sample time: %s' % self.nextSampleTime)

        if self.nextSampleTime and self.currentTime()>=self.nextSampleTime:
            self.samplingCycle()
            self.nextSampleTime = self.computeNextSampleTime()

    def goingOnToOff(self):
        # Filled in by child class
        self.on=False

    def isOn(self):
        # Filled in by child class
        return self.on

    def isOff(self):
        return not self.isOn()

    def checkFlag(self,filename):
        return filename and os.path.exists(filename)

    def checkWindow(self):
        # Derived classes extend this for more interesting logic.
        return self.curSchedule.inWindow()

    def scheduler(self):

        self.nextSampleTime = None

        while True:

            yield True

            if self.schedules.reload(self.scheduleFiles):
                self.nextSampleTime=None

            self.curSchedule = self.schedules.match(self.currentTime())

            if self.curSchedule:
                inWindow = self.curSchedule.enabled and self.checkWindow()
            else:
                inWindow = False

            if self.checkFlag(self.windowFlag):
                inWindow = True

            on = self.isOn()

            if not on and not inWindow:
                self.whenOff()
                continue

            if not on and inWindow:
                self.log.info('Going on')
                self.goingOffToOn()
                if self.curSchedule and self.curSchedule.sampleAtStart:
                    self.nextSampleTime = self.currentTime()
                else:
                    self.nextSampleTime = self.computeNextSampleTime()
                continue

            if on and inWindow:
                self.whenOn()
                continue

            if on and not inWindow:
                self.log.info('Going off')
                self.goingOnToOff()
                continue

    def getDataTime(self, data):
        """Can be filled in by child classes for a time derived by the data."""
        return time.time()

    def getSampleTime(self):
        # Unix timestamp
        return self._sampleTime

    def setSampleTime(self, timestamp):
        if isinstance(timestamp, datetime.datetime):
            timestamp = time.mktime(timestamp.timetuple())
        self._sampleTime = timestamp

    def samplingCycle(self):

        try:
            self.setSampleTime(time.time())
            data = self.sample()
            if data is not None and self.outputenabled:
                self.saveData(self.getSampleTime(),data)
                self.compressFile()
        except:
            self.log.exception('Failed to collect data')

    def step(self):

        try:
            self.startup()
            startupOK=True
        except:
            self.log.exception('Failed to startup')
            startupOK=False

        if startupOK:
            try:
                for k in self.scheduler():
                    yield 1
            except GeneratorExit:
                self.log.info('generator exiting')
            except:
                self.log.exception('Failed in schedule')

        try:
            self.shutdown()
        except:
            self.log.exception('Failed to shutdown')

    def saveData(self,timestamp,buffer):

        ts = time.localtime(self.getInterval(timestamp))
        filename = time.strftime(self.outputname,ts)

        pathname = os.path.dirname(filename)
        if pathname and not os.path.exists(pathname):
            os.makedirs(pathname)

        with open(filename,'a') as output:
            self.write(output,timestamp,buffer)
            output.flush()
            os.fsync(output.fileno())

        mode = stat.S_IRUSR|stat.S_IWUSR|stat.S_IRGRP|stat.S_IWGRP|stat.S_IROTH
        os.chmod(filename,mode)

        self.log.debug(filename)

    def compressFile(self):

        ext = os.path.splitext(self.outputname)[1]
        prefix = os.path.basename(self.outputname).split('-')[0]
        files = glob.glob(prefix+'*'+ext)
        files.sort()

        if self.outputrate:
            files = files[:-1]

        for filename in files:
            data = open(filename).read()
            outputname = os.path.join(self.outputpath,filename)

            if self.compress:
                zdata = bz2.compress(data)
                outputname += '.bz2'
                action = 'compressing'
            else:
                zdata = data
                action = 'copying'

            dirname = os.path.dirname(outputname)
            if not os.path.exists(dirname):
                os.makedirs(dirname)

            with open(outputname,'wb') as output:
                output.write(zdata)
                output.flush()
                os.fsync(output.fileno())

            self.log.info('%s %s: %d -> %d' % \
                (action,filename,len(data),len(zdata)))
            os.remove(filename)

class DataMonitorComponent(ConfigComponent,DataMonitorMixin):

    def __init__(self,*pos,**kw):
        ConfigComponent.__init__(self,'monitor',*pos,**kw)
        DataMonitorMixin.__init__(self)

        self.currentTime = self.parent.currentTime

class DataMonitor(ProcessClient,DataMonitorMixin):

    def __init__(self,argv):
        ProcessClient.__init__(self,argv)
        DataMonitorMixin.__init__(self)

        self.scheduleRate = self.getDeltaTime('schedule.rate',60)

    def run(self):

        for step in self.step():
            if not self.wait(self.scheduleRate,sync=True):
                break

if __name__ == '__main__':
    DataMonitor(sys.argv).run()

