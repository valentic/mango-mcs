#!/usr/bin/env python

###########################################################################
#
#   ATIK Camera Interface
#
#   2021-07-11  Todd Valentic
#               Initial implementation
#
#   2021-07-23  Todd Valentic
#               Add label config parameter
#
#   2025-12-18  Todd Valentic
#               Streamline image capture - match SDK example program
#               Add log parameter, log capture steps
#
###########################################################################

import ctypes
import sys
import datetime
import time
import logging

from collections import OrderedDict
from dotdict import DotDict

SUPPORTED_API_VERSIONS=[20200904]
SUPPORTED_DLL_VERSIONS=[20200904]

CAMERA_ERROR = -1
CAMERA_IDLE = 0
CAMERA_WAITING = 1
CAMERA_EXPOSING = 2
CAMERA_READING = 3
CAMERA_DOWNLOADING = 4
CAMERA_FLUSHING = 5

# Artemis Error Codes (page 14, "AtikCameras.dll Guide.pdf")

ErrorCatalog= {
    0: ('ARTEMIS_OK', 'The function call has been successful'),
    1: ('ARTEMIS_INVALID_PARAMETER', 'One or more of the parameters supplied are inconsistent with what was expected. One example of this would be calling any camera function with an invalid ArtemisHandle. Another might be to set a subframe to an unreachable area.'),
    2: ('ARTEMIS_NOT_CONNECTED', 'Returned when calling a camera function on a camera which is no longer connected.'),
    3: ('ARTEMIS_NOT_IMPLEMENTED', 'Returned for functions that are no longer used.'),
    4: ('ARTEMIS_NO_RESPONSE', 'Returned if a function times out for any reason'),
    5: ('ARTEMIS_INVALID_FUNCTION', "Returned when trying to call a camera specific function on a camera which doesn't have that feature. (Such as the cooling functions on cameras without cooling)."),
    6: ('ARTEMIS_NOT_INITIALISED', "Returned if trying to call a function on something that hasn't been initialised. The only current example is the lens control"),
    7: ('ARTEMIS_OPERATION_FAILED', 'Returned if a function'),
}


class CameraError(Exception):
    pass

class CameraDevice:

    def __init__(self, controller, device, config=None, log=None):

        self.controller = controller
        self.driver = controller.driver
        self.device = device
        self.log = log or logging

        self.reset_config(config)
        self._reset_state()

    #-- Internal methods -------------------------------------------------

    def __del__(self):
        """Make sure that we tell teh camera CCD to warm up."""

        #if self.handle:
        if self.is_connected():
            self.warmup()
            self.disconnect()

    def _reset_state(self):

        self.handle = None
        self.device_name = ''
        self.device_serial = ''
        self.camera_serial = 0

        self.ccd_temp = -273 
        self.ccd_temp_at = datetime.datetime.utcnow() 

        self.image_data = None 
        self.image_start_time = None 

    def _raise_if_error(self, code):

        if code == 0:
            return True

        title, desc = ErrorCatalog.get(code,'Unknown error: %s' % code)

        msg = '%d - %s: %s' % (code, title, desc)

        raise CameraError(msg)

    #-- High-level methods ------------------------------------------------

    def open(self):

        if not self.is_connected():
            self.connect()

        self.device_name = self.get_device_name()
        self.device_serial = self.get_device_serial()
        self.camera_serial = self.get_camera_serial()

        self.initialize()

    def close(self):
        
        if self.is_connected():
            self.warmup()
            self.disconnect()
            self._reset_state()

    def initialize(self, config=None):
        """Initialize camera operating parameters (temperature, binning)."""

        if config:
            self.update_config(config)

        self.set_temperature(self.config.set_point)
        self.set_binning(self.config.bin_x, self.config.bin_y)

    def reset_config(self, config=None):

        defaults = {
            'bin_x':        1,
            'bin_y':        1,
            'set_point':    -5,
            'label':        ''
            }

        self.config = DotDict(defaults)

        if config:
            self.update_config(config)

    def update_config(self, config):
        self.config.update(config)

    def capture_image(self, exposure_time):
        """Take a pictuer. This involves setting the exposure, waiting
           and downloading the image from the camera.

           Parameters
           ==========
           exposure_time : float
               The exposure time in seconds.
        """

        # Minimum exposure time for Atik series 3 and 4 cameras
        # according to the Atik series 3 manual page 8 and series 4
        # manual page 7.

        if exposure_time < 0.001:
            raise ValueError('Minimum exposure time is 0.001 seconds')

        # Check is camera is ready

        if self.get_camera_state() != CAMERA_IDLE:
            raise IOError('Camera is busy')

        # Start the exposure

        camera_state = None 
        deadline = time.time()+exposure_time+180
        start_time = datetime.datetime.utcnow()
        self.start_exposure(exposure_time)

        # Wait until we are done or timeout

        while not self.image_ready():
            if time.time() > deadline:
                raise IOError("Camera timeout")

            new_state = self.get_camera_state()

            if new_state != camera_state:
                camera_state = new_state

                if camera_state == CAMERA_ERROR:
                    self.log.info("Camera error detected")
                    raise IOError("Camera error")

                elif camera_state == CAMERA_IDLE:
                    self.log.info("  idle")

                elif camera_state == CAMERA_WAITING:
                    self.log.info("  waiting")

                elif camera_state == CAMERA_EXPOSING:
                    self.log.info("  exposing")

                elif camera_state == CAMERA_READING:
                    self.log.info("  reading")

                elif camera_state == CAMERA_DOWNLOADING:
                    self.log.info("  downloading")

                elif camera_state == CAMERA_FLUSHING:
                    self.log.info("  flushing")

            time.sleep(0.1)

        self.log.info("  image ready")

        now = datetime.datetime.utcnow()
        total_capture_time = (now - start_time).total_seconds()

        # Read the image data from the camera

        image_data = self.get_image()

        image_data.start_time = start_time
        image_data.capture_time = total_capture_time
        image_data.exposure_time = exposure_time

        return image_data
     
    #-- Artemis methods --------------------------------------------------

    def is_connected(self):
        """Check if we are already connected to the camera."""

        if self.handle is None:
            return False
        else:
            return bool(self.driver.ArtemisIsConnected(self.handle))

    def connect(self):

        self.handle = self.driver.ArtemisConnect(ctypes.c_int(self.device))

        if not self.handle:
            raise IOError('Failed to connect to camera at %d' % self.device)

        return True 

    def disconnect(self):
            
        if not self.handle:
            return

        self.driver.ArtemisDisconnect(self.handle)

        self.handle = None

    def get_device_name(self):
        return self.controller.get_device_name(self.device)
        
    def get_device_serial(self):
        return self.controller.get_device_serial(self.device)

    def get_camera_serial(self):

        flags = ctypes.c_int(0)
        serial = ctypes.c_int(0)

        code = self.driver.ArtemisCameraSerial(
            self.handle, 
            ctypes.byref(flags),
            ctypes.byref(serial)
            )

        self._raise_if_error(code)
                
        return serial.value

    def set_temperature(self, temp):
        """Set the target CCD temperature in Celsius."""

        # temp specified in 1/100th of a degree Celcius
        target_temp = ctypes.c_int(int(temp*100)) 

        code = self.driver.ArtemisSetCooling(self.handle, target_temp)

        return self._raise_if_error(code)

    def get_temperature(self):
        """Read the current CCD temperature."""

        sensor = ctypes.c_int(1)
        temp = ctypes.c_int()

        result = self.driver.ArtemisTemperatureSensorInfo(
            self.handle,
            sensor,
            ctypes.byref(temp)
            )

        self._raise_if_error(result)

        self.ccd_temp = float(temp.value)/100
        self.ccd_temp_at = datetime.datetime.utcnow()

        return self.ccd_temp

    def get_cooling_info(self):

        flags = ctypes.c_int()
        level = ctypes.c_int()
        minlvl = ctypes.c_int()
        maxlvl = ctypes.c_int()
        setpoint = ctypes.c_int()

        result = self.driver.ArtemisCoolingInfo(self.handle,
            ctypes.byref(flags),
            ctypes.byref(level),
            ctypes.byref(minlvl),
            ctypes.byref(maxlvl),
            ctypes.byref(setpoint)
            )

        self._raise_if_error(result)

        return dict(
            flags = flags.value,
            level = level.value,
            minlvl = minlvl.value,
            maxlvl = maxlvl.value,
            setpoint = setpoint.value
        )

    def set_binning(self, x, y):
        """Set the CCD binning factor.
        """

        binx = ctypes.c_int(x)
        biny = ctypes.c_int(y)

        code = self.driver.ArtemisBin(self.handle, binx, biny)

        return self._raise_if_error(code)

    def get_camera_state(self):
        state = self.driver.ArtemisCameraState(self.handle)
        if state == CAMERA_ERROR:
            raise IOError('Error communicating with camera')
        return state 

    def start_exposure(self, exposure_time):
        seconds = ctypes.c_float(exposure_time)
        result = self.driver.ArtemisStartExposure(self.handle, seconds)
        return self._raise_if_error(result)

    def stop_exposure(self):
        self.driver.ArtemisStopExposure(self.handle)

    def image_ready(self):
        return self.driver.ArtemisImageReady(self.handle) 

    def get_image(self):

        x = ctypes.c_int()
        y = ctypes.c_int()
        w = ctypes.c_int()
        h = ctypes.c_int()
        bin_x = ctypes.c_int()
        bin_y = ctypes.c_int()

        results = self.driver.ArtemisGetImageData(
                    self.handle,
                    ctypes.byref(x),
                    ctypes.byref(y),
                    ctypes.byref(w),
                    ctypes.byref(h),
                    ctypes.byref(bin_x),
                    ctypes.byref(bin_y)
                    )

        self._raise_if_error(results)

        image_pointer = self.driver.ArtemisImageBuffer(self.handle)

        if image_pointer==0:
            raise RuntimeError('Problem accessing the image buffer')

        image_size = w.value * h.value
        image_buffer = (ctypes.c_ushort * image_size)()
        bytes_per_pixel = 2   # Atik series 3 and 4 pad ADC to 16-bits

        image_bytes = image_size * bytes_per_pixel
        ctypes.memmove(image_buffer, image_pointer, image_bytes)

        image_data = DotDict(dict(
            x = x.value,
            y = y.value,
            w = w.value,
            h = h.value,
            bin_x = bin_x.value,
            bin_y = bin_y.value,
            image_size = image_size,
            image_bytes = image_bytes, 
            bytes_per_pixel = bytes_per_pixel,
            image_buffer = image_buffer,
            ccd_temp = self.get_temperature(),
            set_point = self.config.set_point,
            label = self.config.label
        ))

        return image_data

    def warmup(self):

        code = self.driver.ArtemisCoolerWarmUp(self.handle)

        return self._raise_if_error(code) 

class CameraController:

    def __init__(self,library='./libatikcameras.so', log=None):

        self.log = log or logging
        self.driver = self._load_driver(library) 

    #-- Internal methods -------------------------------------------------

    def _load_driver(self, library):

        driver = ctypes.CDLL(library)

        self.api_version = driver.ArtemisAPIVersion()
        if self.api_version not in SUPPORTED_API_VERSIONS:
            raise ValueError('Unsupported API version: %S' % self.api_version)

        self.dll_version = driver.ArtemisAPIVersion()
        if self.dll_version not in SUPPORTED_DLL_VERSIONS:
            raise ValueError('Unsupported DLL version: %s' % self.dll_version)

        # Set return types

        driver.ArtemisImageBuffer.restype = ctypes.c_void_p
        driver.ArtemisImageReady.restype = ctypes.c_bool

        return driver
        
    #-- Public methods ---------------------------------------------------

    def scan_for_cameras(self):
        """Scan for Atik cameras and their device serial numbers."""

        num_devices = self.driver.ArtemisDeviceCount()

        devices = [k for k in range(num_devices) if self.is_camera(k)]

        cameras = OrderedDict()

        for device in devices:
            cameras[device] = self.get_device_serial(device)

        return cameras 

    def is_camera(self, device):
        device = ctypes.c_int(device)
        return self.driver.ArtemisDeviceIsCamera(device)
 
    def get_device_name(self, device):

        name = ctypes.create_string_buffer(40)

        self.driver.ArtemisDeviceName(ctypes.c_int(device), name)

        return name.value
        
    def get_device_serial(self, device):

        serial = ctypes.create_string_buffer(40)

        self.driver.ArtemisDeviceSerial(ctypes.c_int(device), serial)

        return serial.value

    def connect(self, device, config=None):
            
        if not self.is_camera(device):
            raise IOError('Device at %d is not a camera' % device)

        return CameraDevice(self, device, config=config, log=self.log) 

    def connect_by_name(self, name, config=None):

        camera_serial_list = self.camera_map.keys()
        camera_list = self.camera_map.values()

        if name not in camera_list:
            raise ValueError('Camera %s not active' % name)

        index = camera_list[name]
        serial = camera_serial_list[index]

        device_map = self.scan_for_cameras()

        device_list = device_map.keys()
        device_serial_list = device_map.values()

        if serial not in device_serial_list:
            raise ValueError('Unknown device with serial %s' % serial)

        index = device_serial_list[serial]
        device = device_list[index]

        return self.connect_by_device(device, config)


if __name__ == '__main__':

    import struct

    if len(sys.argv)==2:
        exposure_time = float(sys.argv[1])
    else:
        exposure_time = 0.001
        
    controller = CameraController()

    print('API Version: %s' % controller.api_version)
    print('DLL Version: %s' % controller.dll_version)
    print('Exposure   : %s' % exposure_time)

    devices = controller.scan_for_cameras()

    print('Found %d cameras at: %s' % (len(devices),devices.keys()))

    if not devices:
        sys.exit(0)

    config = dict(bin_x=1, bin_y=1, temp=-5)
    
    cameras = [controller.connect(device, config) for device in devices]

    for camera in cameras:

        camera.open()

        print('Camera device #%s' % camera.device)
        print('  device name  : %s' % camera.device_name)
        print('  device serial: %s' % camera.device_serial)
        print('  camera serial: %s' % camera.camera_serial)

        end = datetime.datetime.utcnow() + datetime.timedelta(seconds=60)

        deadline = time.time()+30
        while camera.get_temperature() > -4: 
            print('  CCD temp     : %.1fC' % camera.get_temperature())
            time.sleep(5)
            if time.time() > deadline:
                break

        print('Taking image (%s secs)' % exposure_time)
        image_data = camera.capture_image(exposure_time)
        print('  - image size: %d x %d' % (image_data.w, image_data.h))
        print('  - image bytes: %d bytes' % (image_data.image_size*image_data.bytes_per_pixel))
        print('  - capture time: %s' % image_data.capture_time)
        print('  - buffer: %s' % type(image_data.image_buffer))

        with open('/tmp/tmp.dat','w') as output:
            output.write(struct.pack('!%dH' % image_data.image_size, *image_data.image_buffer))

        print('Warm up camera')
        camera.warmup()

        deadline = time.time()+30
        while camera.get_temperature() < 20: 
            print('  CCD temp     : %.1fC' % camera.get_temperature())
            time.sleep(5)
            if time.time() > deadline:
                break

    for camera in cameras:
        camera.close()

    sys.exit(0)

