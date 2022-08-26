#!/usr/bin/env python

##########################################################################
#
#   Manage monitor processes running in threads
#
#   2021-07-11  Todd Valentic
#               Initial implementation 
#
#   2021-07-27  Todd Valentic
#               Fixed issue with camera mapping. The return from scan
#                   cameras had changed to be an OrderedDict
#
#   2022-04-13  Todd Valentic
#               Keep MonitorThread running on exception, don't exit
#
##########################################################################

from Transport import ProcessClient
from Transport import DirectoryMixin, AccessMixin
from CameraController import CameraController
from threading import Thread, Event

from monitor import CameraMonitor

import sys
import os
import json

class MonitorThread(Thread, AccessMixin):

    def __init__(self, name, parent, manager=None, event=None):
        Thread.__init__(self,name=name)
        AccessMixin.__init__(self, parent)

        self.manager = manager
        self.stop_event = event

    def run(self):

        while not self.stop_event.is_set():

            try:
                self._run()
            except:
                self.log.exception('Problem detected')
                self.wait(1)

        self.log.info('Exiting %s' % self.name)

    def _run(self):

        monitor = CameraMonitor(self.manager, self.name, self.parent, 
                                useComponentLog=False)
        secs = 5

        for step in monitor.step():
            if self.stop_event.is_set():
                monitor.shutdown()
                break
            self.wait(secs, sync=True)
            self.log.debug('mark')

class MonitorManager(ProcessClient, DirectoryMixin):

    def __init__(self,argv):
        ProcessClient.__init__(self, argv)
        DirectoryMixin.__init__(self)

        self.pollrate = self.getRate('pollrate',60)
        self.atik_library = self.get('atik_library','./libatikcameras.so')

        self.camera_map_cache = 'camera_map.json'
        self.camera_map = {} 

        self.stop_event = Event()

        self.pductl = self.connect('pductl')
        self.resources = self.connect('resources')

        try:
            self.controller = CameraController(library=self.atik_library)
        except:
            self.log.exception('Failed to create camera controller')
            self.abort()

        args = dict(event=self.stop_event, manager=self)
        self.monitors = self.getComponentsDict('monitors', MonitorThread, **args)

    def connect_camera(self, name, config=None):

        try:
            serial = [v for k,v in self.camera_map.items() if k==name][0]
        except:
            raise ValueError('Camera %s not active' % name)

        device_map = self.controller.scan_for_cameras()

        try:
            device = [k for k,v in device_map.items() if v==serial][0]
        except:
            raise ValueError('Unknown device with serial %s' % serial)

        return self.controller.connect(device, config)

    def get_device_serial(self):
        
        devices = self.controller.scan_for_cameras()

        if len(devices) == 0:
            return None
        elif len(devices) > 1:
            self.log.error('Multiple camera devices are present')
            return None

        return devices.values()[0] 

    def map_cameras(self):

        self.log.info('Mapping cameras')

        device_state = self.pductl.getDeviceState()

        # Only works if all cameras are off
    
        cameras_on = [name for name in device_state if device_state[name]=='on']

        if cameras_on:
            self.log.error('  - found cameras already on: %s' % ' '.join(cameras_on)) 
            return False

        # Power on each camera one at a time, read serial number 

        self.camera_map = {}

        for camera_name in self.monitors:
            self.resources.allocate('camera_manager',['%s=on' % camera_name])    
            self.wait(3)
            try:
                serial = self.get_device_serial()
            except:
                self.log.exception('Failed to read serial number for %s' % camera_name)
                serial=None
            self.resources.allocate('camera_manager',[])

            if serial:
                self.camera_map[camera_name] = serial
                self.log.info('  - %s => %s' % (camera_name, serial))
            else:
                self.log.info('  - %s =>' % camera_name)

            if not self.running:
                return False

        # Save map into cache file

        with open(self.camera_map_cache,'w') as output:
            output.write(json.dumps(self.camera_map))

        return True

    def get_camera_mapping(self):

        # Use the current mapping file exists and there are no errors
        
        if os.path.exists(self.camera_map_cache):
            try:
                self.camera_map = json.loads(open(self.camera_map_cache).read())
                return True
            except:
                self.log.exception('Problem reading mapping file')

        # Otherwise, map the cameras 
    
        return self.map_cameras()

    def has_camera_map(self):

        while not self.get_camera_mapping():
            if not self.running:
                return False
            self.log.info('  - try again in 10 seconds') 
            self.wait(10)

        self.log_camera_map()

        return True

    def log_camera_map(self):

        self.log.info('Camera to serial mapping:')

        for camera, serial in self.camera_map.items():
            self.log.info('  %s => %s' % (camera, serial))

    def start_cameras(self):

        active_cameras = set(self.camera_map).intersection(self.monitors)

        for camera_name in active_cameras:
            self.log.info('Starting %s thread' % camera_name) 
            self.monitors[camera_name].start()

        return active_cameras

    def stop_cameras(self, active_cameras):

        self.log.info('Waiting for camera threads to finish')

        self.stop_event.set()

        for camera_name in active_cameras: 
            self.monitors[camera_name].join()

    def run(self):

        if not self.has_camera_map():
            return 

        active_cameras = self.start_cameras() 

        while self.wait(self.pollrate):
            self.log.debug('mark') 

        self.stop_cameras(active_cameras)

        self.log.info('Finished')

if __name__ == '__main__':
    MonitorManager(sys.argv).run()
