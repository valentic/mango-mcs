#!/usr/bin/env python2

###################################################################
#
#   Camera Data Monitor
#
#   Capture images from the camera 
#
#   2021-04-01  Todd Valentic
#               Initial implementation
#
#   2021-07-23  Todd Valentic
#               Add camera.label parameter
#
#   2021-07-25  Todd Valentic
#               Add warmup and cooldown parameters
#
#   2021-08-09  Todd Valentic
#               Extend cache timeout to be 10-minutes
#               Make sure to clear prev_camera_config when off
#               Add some debug log entries 
#
#   2021-08-12  Todd Valentic
#               Add instrument name to output
#
#   2021-08-17  Todd Valentic
#               Protect status call in isOn()
#
#   2021-09-22  Todd Valentic
#               Reset settings cache when connecting to camera
#
#   2022-04-18  Todd Valentic
#               Set/clear keepalive flag
#
#   2022-05-02  Todd Valentic
#               Use image timestamp for sampleTime
#               Shutdown on timeout (results in a restart)
#
#   2022-05-05  Todd Valentic
#               Shutdown if problem connecting to camera
#
###################################################################

from NightDataMonitor import NightDataMonitorComponent
from Transport.Util import datefunc

import os
import sys
import time
import struct
import ConfigParser
import StringIO

class CameraMonitor(NightDataMonitorComponent):

    def __init__(self, manager, *pos, **kw):
        NightDataMonitorComponent.__init__(self, *pos, **kw)

        self.manager = manager 
        self.camera = None
        self.camera_config = None
        self.prev_camera_config = None

        self.keepalive_flag = self.get('keepalive_flag')

        self.station = self.get('station','Unknown')
        self.cacheService.set_timeout(self.name,60*10)

    def getParameters(self, schedule):

        # Defaults from transport config file

        exposure_time = self.getDeltaTime('camera.exposure_time',10)
        warmup_time = self.getDeltaTime('camera.warmup_time',60*3)
        cooldown_time = self.getDeltaTime('camera.cooldown_time',60*2)
        
        bin_x = self.getint('camera.bin_x',1)
        bin_y = self.getint('camera.bin_y',1)
        set_point = self.getfloat('camera.set_point',-5)
        label = self.get('camera.label',self.name)

        diskFree = self.getBytes('require.diskFree','1MB')
        diskMount = self.get('require.diskMount','/')

        # Override from schedule

        if schedule:

            exposure_time = schedule.getDeltaTime('camera.exposure_time',exposure_time)
            warmup_time = schedule.getDeltaTime('camera.warmup_time',warmup_time)
            cooldown_time = schedule.getDeltaTime('camera.cooldown_time',cooldown_time)

            bin_x = schedule.getint('camera.bin_x',bin_x)
            bin_y = schedule.getint('camera.bin_y',bin_y)
            set_point = schedule.getint('camera.set_point',set_point)
            label = schedule.get('camera.label',label)

            diskFree = schedule.getBytes('require.diskFree',diskFree)
            diskMount = schedule.get('require.diskMount',diskMount)

        # Set

        self.exposure_time = exposure_time.total_seconds()
        self.warmup_time = warmup_time 
        self.cooldown_time = cooldown_time

        self.camera_config = dict(
            bin_x = bin_x, 
            bin_y = bin_y, 
            set_point = set_point, 
            label = label 
        )

        self.diskFree = diskFree 
        self.diskMount = diskMount 

        if schedule:
            self.log.info('schedule: %s' % schedule.name)
        else:
            self.log.info('schedule: None')

        self.log.debug('camera config: %s' % self.camera_config)

    def isOn(self):
        status = self.getStatus()
        try:
            return status.get('device',self.name)=='on'
        except:
            return False

    def startup(self):
        
        if self.isOn():
            self.goingOffToOn()

    def shutdown(self):

        if self.isOn():
            self.goingOnToOff()

    def set_flag(self, filename):   

        if not filename:
            return

        filepath = os.path.split(os.path.abspath(filename))[0]

        if not os.path.isdir(filepath):
            try:
                os.mkdir(filepath)
            except:
                pass

        self.log.info('Setting keepalive flag')

        with open(filename,'a'):
            pass

    def clear_flag(self, filename):

        if filename and os.path.exists(filename):
            self.log.info('Clearing keepalive flag')
            os.remove(filename)

    def goingOffToOn(self):

        self.setResources('%s=on' % self.name)

        self.set_flag(self.keepalive_flag)

        # Wait for device to come online
        self.wait(5)

        try:
            self.camera = self.manager.connect_camera(self.name)
            self.camera.open()
        except:
            self.log.exception('Problem connecting to camera')
            self.clear_flag(self.keepalive_flag)
            self.clearResources()
            return

        self.camera_config = None
        self.prev_camera_config = None

        self.log.info('Connected to camera')

        self.getParameters(self.curSchedule)
        self.update_camera_config()

        self.log.info('Cooling down for %s' % self.cooldown_time)
        self.monitor_camera(self.cooldown_time, 'Cool')

    def goingOnToOff(self):

        self.getParameters(self.curSchedule)

        if self.camera:
            self.log.info('Disconnecting from camera')

            # Warm up the camera before powering off
            # Use time.sleep() since wait() will exit if we are stopped

            self.log.info('Warming up for %s' % self.warmup_time)
            self.camera.warmup()

            self.monitor_camera(self.warmup_time, 'Warm', hard=True)

            self.cacheService.clear(self.name)
            self.camera.close()
            self.camera = None
            self.camera_config = None
            self.prev_camera_config = None
            
        self.clear_flag(self.keepalive_flag)

        self.clearResources()

    def shutdown(self):

        if self.isOn():
            self.goingOnToOff()

    def update_camera_config(self):

        # Only download to camera if different 

        if self.camera_config != self.prev_camera_config:
            self.log.info('Updating camera configuration: %s' % self.camera_config)
            self.camera.initialize(config=self.camera_config)
            self.prev_camera_config = self.camera_config

    def monitor_camera(self, totaltime, label, waitsecs=15, hard=False):

        # hard = ensure totalime is met, otherwise exit
        # if we are not running anymore

        endtime = self.currentTime() + totaltime

        while self.currentTime() < endtime:
            ccd_temp = self.report_temperature(label)
            self.log.info('  - CCD temp %5.1fC' % ccd_temp)

            if hard:
                time.sleep(waitsecs)
            elif not self.wait(waitsecs):
                break

    def report_temperature(self, state):
        ccd_temp = self.camera.get_temperature()
        self.putCache(self.name,dict(ccd_temp=ccd_temp, state=state))
        return ccd_temp

    def ready(self):

        # Update camera config if changed 

        self.update_camera_config()

        # Check system conditions such as disk space

        systemStatus = ConfigParser.ConfigParser()

        try:
            buffer = StringIO.StringIO(self.getCache('system'))
            systemStatus.readfp(buffer)
        except: 
            self.log.exception('Failed to get system status from cache')
            return False

        if not systemStatus.has_section(self.diskMount):
            self.log.info('Skipping - data mount missing')
            return False

        freeBytes = systemStatus.getint(self.diskMount,'freebytes')

        self.log.debug('Disk free: %s, min: %s' % (freeBytes,self.diskFree))

        if freeBytes < self.diskFree:
            self.log.info('Skipping - not enough disk space')
            return False
            
        return True

    def sample(self):
        
        self.getParameters(self.curSchedule)

        if not self.ready():
            return None

        self.log.info('Starting image capture')

        try:
            image_data = self.camera.capture_image(self.exposure_time) 
        except:
            self.log.exception('Problem capturing image')
            self.shutdown()
            return None

        summary = '%.1fs / %.1fs / %.0fKB / %.1fC' % (
            image_data.exposure_time,
            image_data.capture_time,
            image_data.image_bytes / 1024,
            image_data.ccd_temp)

        self.log.info('  %s' % summary) 
        self.report_temperature('Run')

        location = self.location.best()

        image_data.latitude = location['latitude']
        image_data.longitude = location['longitude']
        image_data.instrument = self.name

        self.log.debug('image_data.bin_x: %s' % image_data.bin_x)
        self.log.debug('image_data.bin_y: %s' % image_data.bin_y)
        self.log.debug('image_data.label: %s' % image_data.label)

        self.setSampleTime(image_data.start_time)

        return image_data 

    def write(self, output, timestamp, data):

        version = 3

        start_time = int(datefunc.datetime_as_seconds(data.start_time))

        output.write(struct.pack('!B',version))
        output.write(struct.pack('!i',start_time))
        output.write(struct.pack('!40s',self.station))
        output.write(struct.pack('!f',data.latitude))
        output.write(struct.pack('!f',data.longitude))
        output.write(struct.pack('!i',self.camera.camera_serial))
        output.write(struct.pack('!40s',self.camera.device_name))
        output.write(struct.pack('!40s',data.label))
        output.write(struct.pack('!40s',data.instrument))
        output.write(struct.pack('!f',data.exposure_time))
        output.write(struct.pack('!i',data.x))
        output.write(struct.pack('!i',data.y))
        output.write(struct.pack('!i',data.w))
        output.write(struct.pack('!i',data.h))
        output.write(struct.pack('!i',data.bytes_per_pixel))
        output.write(struct.pack('!i',data.bin_x))
        output.write(struct.pack('!i',data.bin_y))
        output.write(struct.pack('!f',data.ccd_temp))
        output.write(struct.pack('!f',data.set_point))
        output.write(struct.pack('!i',data.image_bytes))
        output.write(struct.pack('!%dH' % data.image_size,*data.image_buffer))

if __name__ == '__main__':
    CameraMonitor(sys.argv).run()
