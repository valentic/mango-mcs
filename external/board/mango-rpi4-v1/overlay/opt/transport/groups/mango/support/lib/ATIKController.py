#!/usr/bin/env python
#####################################################################
#
#   Camera Control Class MANGO (Atik 314L+ and 414EX)
#   (Midlatitude All-sky-imager Network for Geophysical Observation)
#
#   Note that this code was only test using Python 3.6 or newer and
#   depends on astropy>=4.0.1.post1 numpy>=1.19.2
#
#   2013-02-01  Rohit Mundra/Fabrice Kengne
#               Initial implementation
#
#   2013-02-21  Fabrice Kengne
#               Added PyFITS support
#
#   2020-02-28  Ashton Reimer
#               Refactored, python3 support
#
#   2020-09-11  Ashton Reimer
#               Completed rewrite. Tested against 414EX. Improved
#               documentation.
#
#   2020-09-29  Ashton Reimer
#               Removed FITS support. Added mock for testing.
#               Class initialized with config file.
#
#   2021-03-31  Todd Valentic
#               Remove non-ASCII characters that got copy-pasted in
#
#   2021-04-07  Todd Valentic
#               Add logging 
#               Use ConfigParser getint, getfloat
#
#   2021-07-09  Todd Valentic
#               Need to specify the return types on some functions
#                   to handle non-int (i.e. void*) values
#               Make sure default value for CCD temp is [date, temp]
#               Query device name and serial number on connect
#
#####################################################################


import os
import time
import ctypes
import logging

from glob import glob
from datetime import datetime

try:
    import ConfigParser  #python 2
except ImportError:
    import configparser as ConfigParser  #python3

import numpy as np

SUPPORTED_API_VERSIONS = [20200904]
SUPPORTED_DLL_VERSIONS = [20200904]
DEVICE = ctypes.c_int(0)


# Artemis Error Codes (page 14, "AtikCameras.dll Guide.pdf")
ERROR_CODES = {0: ('ARTEMIS_OK', 'The function call has been successful'),
               1: ('ARTEMIS_INVALID_PARAMETER', 'One or more of the parameters supplied are inconsistent with what was expected. One example of this would be calling any camera function with an invalid ArtemisHandle. Another might be to set a subframe to an unreachable area.'),
               2: ('ARTEMIS_NOT_CONNECTED', 'Returned when calling a camera function on a camera which is no longer connected.'),
               3: ('ARTEMIS_NOT_IMPLEMENTED', 'Returned for functions that are no longer used.'),
               4: ('ARTEMIS_NO_RESPONSE', 'Returned if a function times out for any reason'),
               5: ('ARTEMIS_INVALID_FUNCTION', "Returned when trying to call a camera specific function on a camera which doesn't have that feature. (Such as the cooling functions on cameras without cooling)."),
               6: ('ARTEMIS_NOT_INITIALISED', "Returned if trying to call a function on something that hasn't been initialised. The only current example is the lens control"),
               7: ('ARTEMIS_OPERATION_FAILED', 'Returned if a function'),
              }

# ARTEMISPROPERTIES Structure Codes (page 5, "AtikCameras.dll Guide.pdf")
class ArtemisProperties(ctypes.Structure):
    _fields_ = [("Protocol",ctypes.c_int),
                ("nPixelsX",ctypes.c_int),
                ("nPixelsY",ctypes.c_int),
                ("PixelMicronsX",ctypes.c_float),
                ("PixelMicronsY",ctypes.c_float),
                ("ccdflags",ctypes.c_int),
                ("cameraflags",ctypes.c_int),
                ("Description",ctypes.ARRAY(ctypes.c_char,40)),
                ("Manufacturer",ctypes.ARRAY(ctypes.c_char,40))
               ]


class MockedCamera():
    """A class that we can use to fake a camera for testing.
    """
    def __init__(self):
        self.x = 1
        self.y = 1

        self.exposing = False
        self.connected = False

    def ArtemisDisconnect(self,camera_handle):
        self.connected = False
        return 1

    def ArtemisDeviceCount(self):
        return 1

    def ArtemisDeviceIsCamera(self,device):
        return 1

    def ArtemisDeviceName(self,device,name):
        name.value='Atik 414ex'
        return 1 

    def ArtemisDeviceSerial(self,device,serial):
        serial.value='441'
        return 1 

    def ArtemisIsConnected(self,camera_handle):
        return int(self.connected)

    def ArtemisConnect(self,DEVICE):
        self.connected = True
        return 112

    def ArtemisSetCooling(self,camera_handle,temperature):
        return 0

    def ArtemisGetBin(self,camera_handle,x,y):
        x.value = self.x
        y.value = self.y
        return 0

    def ArtemisBin(self,camera_handle,x,y):
        self.x = x.value
        self.y = y.value
        return 0

    def ArtemisTemperatureSensorInfo(self,camera_handle,sensor,passtemp):
        passtemp.value = int(1000*np.random.random())
        return 0

    def ArtemisCoolerWarmUp(self,camera_handle):
        return 0

    def ArtemisCameraState(self,camera_handle):
        if self.exposing:
            self.exposing = False
            return 1
        else:
            return 0

    def ArtemisStartExposure(self,camera_handle,seconds):
        self.exposing = True
        return 0

    def ArtemisImageReady(self,camera_handle):
        return 1

    def ArtemisGetImageData(self,camera_handle,x,y,w,h,bin_x,bin_y):
        w.value = 640
        h.value = 480
        return 0

    def ArtemisImageBuffer(self,camera_handle):
        image_size = 640*480
        rand = 20*np.array([int(x) for x in np.random.randn(image_size)]) + 100
        self.image_buffer = (ctypes.c_ushort*image_size)(*rand.tolist())
        return ctypes.addressof(self.image_buffer)

class ATIKController():
    """A controller class for connecting to and controlling Atik cameras,
    specifically the 414 EX.

    Parameters
    ==========
    config_file : str
        Path to a config file defining several required parameters.
    atik_driver : str
        The path to the libatikcameras.so shared library object.

    Attributes
    ==========
    config_file : str
        Path to a config file defining several required parameters.
    driver : :class: `ctypes.CDLL`
        The ctypes CDLL interface to the libatikcameras.so.
    target_temp : float
        The temperature, in Celsius, that the CCD should be held to. Default
        is -5 C.
    bin_x : int
        The CCD binning in the x axis.
    bin_y : int
        The CCD binning in the y axis.
    camera_handle : int
        The handle of the camera that this controller class is connected to.
    CCD_temp : list
        A list containing a datetime of when a CCD temperature was measured
        and the temperature of the CCD in Celsius.
    last_start_time : :class: `datetime.datetime`
        The time in UTC when the last exposure was started.


    Notes
    =====
    This class was tested for specific versions of the libatikcameras.so, which
    is included in the Atik SDK: https://www.atik-cameras.com/software-downloads/

    This class assumes you only have 1 Atik camera connected to the computer and
    it will only connect to the first camera that you plug in to the computer.

    """
    def __init__(self,config_file, mock=False, log=None):

        if not log:
            log = logging
            logging.basicConfig(level=logging.INFO)

        self.log = log
        self.config_file = config_file
        parser = ConfigParser.ConfigParser()
        parser.read(self.config_file)

        self.target_temp = parser.getfloat('camera','target_temp')
        self.bin_x = parser.getint('camera','bin_x')
        self.bin_y = parser.getint('camera','bin_y')
        atik_driver = parser.get('camera','driver')

        if mock:
            self.driver = MockedCamera()
            def byref(arg):
                return arg
            ctypes.byref = byref
        else:
            # set and check the driver
            self.driver = ctypes.CDLL(atik_driver)
            self.check_driver()

        self.camera_handle = None # No Camera Connected yet
        self.device_name = '' 
        self.device_serial = '' 
        self.camera_flags = 0 
        self.camera_serial = 0 
        self.CCD_temp = [datetime.utcnow(),-273] # Temperature not read yet
        self.last_start_time = 'Not Set' # time when last exposure was started


    def __del__(self):
        """Make sure that we tell the camera CCD to warmup before get deleted.
        """
        self.__warmup()


    def check_driver(self):
        """Check to make sure we are accessing a libatikcameras.so with an API
        and DLL version that we have tested this class against.
        """
        # check the API version of the driver
        api_version = self.driver.ArtemisAPIVersion()
        if not api_version in SUPPORTED_API_VERSIONS:
            raise Exception('API version incompatible: %s' % str(api_version))

        # check the dll version of the driver
        dll_version = self.driver.ArtemisDLLVersion()
        if not dll_version in SUPPORTED_DLL_VERSIONS:
            raise Exception('DLL version incompatible: %s' % str(dll_version))

        # Set the return types

        self.driver.ArtemisImageBuffer.restype = ctypes.c_void_p

    def is_connected(self):
        """Check if we are connected to a camera with handle: camera_handle.
        """
        if self.camera_handle is None:
            return False
        else:
            return bool(self.driver.ArtemisIsConnected(self.camera_handle))


    def command_api(self,command):
        """A method that wraps libatikcameras.so API calls.

        Parameters
        ==========
        command : str
            A shortname for the API command to execute. Available commands
            are: 'disconnect', 'connect', 'settemperature', 'getbinning', 
            'setbinning', 'getCCDtemperature', and 'warmup'
        """
        # default return text is blank but will change if we encouter a problem
        text = ''

        # disconnect from a connected camera
        if command == 'disconnect':
            if not self.camera_handle is None:
                # disconnect
                status = bool(self.driver.ArtemisDisconnect(self.camera_handle))
                # if we successfuly disconnect, reset the camera_handle to None
                if status:
                    self.camera_handle = None
            else:
                status = False

        # try to connect to a camera
        elif command == 'connect':
            try:
                status = self.__connect()
            except Exception as e:
                status = False
                text = 'Failed to connect!\n%s' % str(e)

        # tell the camera to cool the CCD to the target temperature
        elif command == 'settemperature':
            try:
                status = self.__set_temperature()
            except Exception as e:
                status = False
                text = 'Failed to set temperature!\n%s' % str(e)

        # get the CCD pixel binning
        elif command == 'getbinning':
            try:
                status = self.__get_binning()
            except Exception as e:
                status = False
                text = 'Failed to get CCD binning!\n%s' % str(e)

        # set the CCD pixel binning
        elif command == 'setbinning':
            try:
                status = self.__set_binning()
            except Exception as e:
                status = False
                text = 'Failed to set CCD binning!\n%s' % str(e)

        # get the current CCD temperature
        elif command == 'getCCDtemperature':

            try:
                status = self.__get_CCD_temperature()
            except Exception as e:
                status = False
                text = 'Failed to get CCD temperature!\n%s' % str(e)

        # tell the CCD to start warming up again
        elif command == 'warmup':
            try:
                status = self.__warmup()
            except Exception as e:
                status = False
                text = 'Failed to connect!\n%s' % str(e)

        else:
            raise ValueError('Unknown command: %s' % command)

        return status, text


    def __connect(self):
        """Try to connect to an Atik camera that is plugged in to the computer.
        """
        # make sure there is even a camera connected
        num_devices = self.driver.ArtemisDeviceCount()
        if num_devices == 0:
            self.camera_handle = None
            raise Exception('No Artemis device found!')

        # if there is a device, it is at index 0, make sure it is a camera
        is_camera = self.driver.ArtemisDeviceIsCamera(DEVICE)
        if not is_camera:
            raise Exception('Artemis device found, but not a camera!')

        # check if we are already connected
        is_connected = self.is_connected()

        # if we aren't connected, or the handle is none, connect!
        if not is_connected or (self.camera_handle is None):
            # pass -1 to get 
            # returns 0 if it fails.
            handle = self.driver.ArtemisConnect(DEVICE)
            if not handle:
                return False

            self.camera_handle = handle 

            name = ctypes.create_string_buffer(40)
            serial = ctypes.create_string_buffer(40) 

            self.driver.ArtemisDeviceName(DEVICE, name)
            self.driver.ArtemisDeviceSerial(DEVICE, serial)

            self.device_name = name.value
            self.device_serial = serial.value 

            camera_flags = ctypes.c_int(0)
            camera_serial = ctypes.c_int(0)

            pflags = ctypes.byref(camera_flags)
            pserial = ctypes.byref(camera_serial)

            self.driver.ArtemisCameraSerial(self.camera_handle,pflags,pserial)

            self.camera_flags = camera_flags.value
            self.camera_serial = camera_serial.value

            return True

        # otherwise we are already connected
        return True

    def __set_temperature(self):
        """Sets the desired target temperature for the exposure in Celsius.
        """
        # make sure we are connected to the camera
        status, text = self.command_api('connect')
        if not status:
            return status, text

        # set the target CCD temperature
        temperature = ctypes.c_int(int(100*self.target_temp))
        error_code = self.driver.ArtemisSetCooling(self.camera_handle, temperature)

        if error_code != 0:
            text = ERROR_CODES.get(error_code,('Uh Oh','Unknown Error!'))
            raise Exception('%s: %s' % text)
        
        return True

    def __get_binning(self):
        """Gets the binning factor sets for the CCD camera.
        """
        # make sure we are connected to the camera
        status, text = self.command_api('connect')
        if not status:
            return status, text

        x = ctypes.c_int(0)
        y = ctypes.c_int(0)

        error_code = self.driver.ArtemisGetBin(self.camera_handle, ctypes.byref(x), ctypes.byref(y))
        if error_code != 0:
            text = ERROR_CODES.get(error_code,('Uh Oh','Unknown Error!'))
            raise Exception('%s: %s' % text)
        else:
            self.bin_x = int(x.value)
            self.bin_y = int(y.value)

        return True


    def __set_binning(self):
        """Sets the CCD binning factor to bin_x and bin_y.
        """

        # make sure we are connected to the camera
        status, text = self.command_api('connect')
        if not status:
            return status, text

        x = ctypes.c_int(self.bin_x)
        y = ctypes.c_int(self.bin_y)
        error_code = self.driver.ArtemisBin(self.camera_handle, x, y)

        if error_code != 0:
            text = ERROR_CODES.get(error_code,('Uh Oh','Unknown Error!'))
            raise Exception('%s: %s' % text)

        return True


    def __get_CCD_temperature(self):
        """Gets the CCD temperature in Celsius and a timestamp.
        """

        # make sure we are connected to the camera
        status, text = self.command_api('connect')
        if not status:
            return status, text

        sensor = ctypes.c_int(1)
        temp = ctypes.c_int(0)
        passtemp = ctypes.byref(temp)

        error_code = self.driver.ArtemisTemperatureSensorInfo(self.camera_handle, sensor, passtemp)
        if error_code != 0:
            text = ERROR_CODES.get(error_code,('Uh Oh','Unknown Error!'))
            raise Exception('%s: %s' % text)
        else:

            self.CCD_temp = [datetime.utcnow(),float(temp.value)/100]

        return True


    def __warmup(self):
        """Tell the camera to begin warming up the CCD.
        """

        # make sure we are connected to the camera
        status, text = self.command_api('connect')
        if not status:
            return status, text

        # command the CCD to warm up
        error_code = self.driver.ArtemisCoolerWarmUp(self.camera_handle)
        if error_code != 0:
            text = ERROR_CODES.get(error_code,('Uh Oh','Unknown Error!'))
            raise Exception('%s: %s' % text)

        return True

    def initialize(self):
        """ A general camera initialization: connect, set the CCD temperature,
        and set the CCD binning factor.
        """
        self.log.info('Connecting to camera...')
        status, text = self.command_api('connect')
        if not status:
            raise Exception(text)
        self.log.info('Setting CCD target temperature...')
        status, text = self.command_api('settemperature')
        if not status:
            raise Exception(text)
        self.log.info('Setting CCD pixel binning...')
        status, text = self.command_api('setbinning')
        if not status:
            raise Exception(text)

    def take_exposure(self,exposure_time):
        """Take a picture. This involves setting the exposure, waiting,
        and downloading the image from the camera.

        Parameters
        ==========
        exposure_time : float
            The exposure time for the image you wish to capture, in seconds.

        Notes
        =====
        The image is saved as a `numpy` array to the `image_array` attribute.

        This code was based on the methodology followed by the
        'AtikCamerasSDKApp::StartExposure' function in the
        'AtikCamerasSDKApp.cpp' in the Atik SDK examples.
        """

        # Minimum exposure time for Atik series 3 and series 4 cameres
        # according to Atik series 3 manual page 8, and Atik series 4 manual
        # page 7.
        if exposure_time < 0.001:
            raise Exception('Valid minimum exposure time is 0.001 seconds.')

        # Now check to see if the camera is even ready to do an exposure, or if it is busy
        # (page 7 "AtikCameras.dll Guide.pdf")
        status_code = self.driver.ArtemisCameraState(self.camera_handle)
        if status_code == -1:
            raise Exception("Problem encountered while taking exposure.")
        if status_code != 0:
            raise Exception("Camera is not idle!")

        # grab the start time
        start_time = datetime.utcnow()
        # start taking the exposure!
        seconds = ctypes.c_float(exposure_time)
        self.log.info('Starting a %s second exposure at: %s' % (str(exposure_time),str(start_time)))
        error_code = self.driver.ArtemisStartExposure(self.camera_handle, seconds)
        if error_code != 0:
            text = ERROR_CODES.get(error_code,('Uh Oh','Unknown Error!'))
            raise Exception('%s: %s' % text)

        # so the camera should be exposing now! Let's check it's status:
        # Codes are 0=IDLE, 1=WAITING, 2=EXPOSING, 4=DOWNLOADING, -1=ERROR.
        # (page 7 "AtikCameras.dll Guide.pdf")
        status_code = self.driver.ArtemisCameraState(self.camera_handle)

        # If there was an error raise exception
        if status_code == -1:
            raise Exception("Problem encountered while taking exposure.")

        # If still waiting or exposing, let's sleep until we expect it to be done
        if status_code in [1,2]:
            # see how much exposure time is remaining
            seconds_since_start = (datetime.utcnow() - start_time).total_seconds()
            sleep_seconds = exposure_time - seconds_since_start
            if sleep_seconds > 0:
                time.sleep(sleep_seconds)

        # Now we'll wait until the image is downloaded and ready
        self.log.info('Waiting for image to be ready...')
        while not self.driver.ArtemisImageReady(self.camera_handle):
            time.sleep(0.1)

        total_capture_time = (datetime.utcnow()-start_time).total_seconds()
        self.log.info('Took %s seconds to capture the image.' % str(total_capture_time))

        # now we grab the image data from the API.
        x   = ctypes.c_int(0)
        y   = ctypes.c_int(0)
        w   = ctypes.c_int(0)
        h   = ctypes.c_int(0)
        self.driver.ArtemisGetImageData(self.camera_handle,
                                        ctypes.byref(x), ctypes.byref(y),
                                        ctypes.byref(w), ctypes.byref(h),
                                        ctypes.byref(ctypes.c_int(self.bin_x)),
                                        ctypes.byref(ctypes.c_int(self.bin_y)))

        # and grab the pointer to the start of the image buffer
        image_pointer = self.driver.ArtemisImageBuffer(self.camera_handle)
        if image_pointer == 0:  # this should never happen, but check just in case
            raise Exception("Problem encountered while accessing image buffer.")

        # now copy the image from the image buffer and convert to a numpy array
        image_size = w.value*h.value
        image_buffer = (ctypes.c_ushort*image_size)()
        # Atik series 3 and series 4 pad their ADC bits up to 16 bits.
        bytes_per_pixel = 2
        ctypes.memmove(image_buffer, image_pointer, image_size*bytes_per_pixel)
        image_array = np.array(image_buffer[0:image_size]).astype(np.uint16)
        self.image_array = image_array.reshape((h.value, w.value))

        # save the time when we started doing the exposure
        self.last_start_time = start_time



# run a unit test with a mocked camera driver
if __name__ == '__main__':
    import tempfile

    # write a temporary config file
    contents="""[Default]
[camera]
bin_x = 1
bin_y = 1
target_temp = -5
driver = ./libatikcameras.so"""
    with tempfile.NamedTemporaryFile('w',delete=False) as f:
        filename = f.name
        f.write(contents)

    # initialize the controller and run some commands
    a = ATIKController(filename,mock=False)
    a.initialize()

    a.take_exposure(10)
    print("Here's the image data:")
    print(a.image_array)
    print('Get CCD temp')
    a.command_api('getCCDtemperature')
    print(a.CCD_temp)

    # cleanup the config file.
    os.remove(filename)
