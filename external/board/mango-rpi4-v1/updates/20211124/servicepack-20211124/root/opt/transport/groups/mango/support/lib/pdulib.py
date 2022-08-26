#!/usr/bin/env python

###############################################################################
#
#   PDUv2 Hardware Interface
#
#   Commands:
#
#   LD(s)    -- Turns 'on' or 'off' logging to SD card
#   SA(s)    -- Turns 'on' or 'off' automatic sending of sensor data
#                   (RH,RP,RT,RLTC and RBME)
#   ST(iffi) -- Setup Channel, Low, High and Zone for heaters
#                   (settings persists i.e. are stored in flash)
#   SP(is)   -- Sets selected power FET to 'on' or 'off'
#   RTS      -- Reads the heater temperature settings
#   RH       -- Reads the current heater switch states
#   RP       -- Reads the current power switch states (commanded FET state)
#   RS       -- Reads the current power switch states (actual FET state)
#   RT       -- Reads the temperature sensors
#   RLTC     -- Reads the voltage and current outputs
#
#   PDUv2 only:
#   RBME     -- Reads the BME280 sensor (temp, pressure, altitude, humidity)
#
#   PDUsp only:
#
#   SHEN(is) -- Set selected heater to 'e' enabled or 'd' disabled
#   RHEN()   -- Header enable states: 1=enabled, 0=disables (MSB to LSB)
#   VER()    -- Report firmware version
#   UP()     -- Uptime information
#
#   PDUwps only implements:
#       SP
#       RP
#
#   Number of elements
#
#       Name    PDUv2   PDUsp   PDUwps
#       RH      8       4       0
#       RP      11      8       8
#       RS      11      8       8
#       RT      20      8       0
#       RLTC    12      9       0
#
#   Note - the output of RH, RP, RT are MSB...LSB (i.e. 8 7 6 .. 3 2 1)
#
#   2018-05-30  Todd Valentic
#               Initial implementation
#
#   2019-07-02  Todd Valentic
#               Handle the new PDUsp (small payload) version.
#
#   2021-04-05  Todd Valentic
#               Add Digital Logger web power switch model (PDU_webpowerswitch)
#
#   2021-07-21  Todd Valentic
#               Test log for None directly
#
#   2021-08-17  Todd Valentic
#               Add REST API support to PDU_powerswitch 
#
###############################################################################

from ParseKit import parseFloats
from requests.auth import HTTPBasicAuth, HTTPDigestAuth

import lockfile
import socket
import serial
import re
import os
import datetime
#import pytz
import time
import urllib
import requests
import re
import json

from dateutil.tz import UTC

class Timeout(Exception):
    pass

class InterfaceBase(object):
    
    def __init__(self,addr,**args):
        self.addr = addr

    def enter(self):
        pass

    def exit(self):
        pass

    def write(self,msg):
        pass

    def read(self):
        return ''

    def info(self):
        return dict(addr=self.addr)

class HTTPInterface(InterfaceBase):
    
    def __init__(self,addr,auth=None,scheme=None,**args):

        self.auth = auth

        if scheme:
            self.scheme = scheme
        else:
            self.scheme = 'http'
            
        super(HTTPInterface,self).__init__(addr,**args) 

        socket.setdefaulttimeout(5)

        self.result = None

    def write(self, msg, method='GET', data=None, headers=None):

        url = '%s://%s/%s' % (self.scheme, self.addr, msg)

        if method == 'GET':

            self.response = requests.get(url, 
                                        auth=self.auth, 
                                        headers=headers, 
                                        params=data)
        elif method == 'PUT':

            self.response = requests.put(url, 
                                        auth=self.auth, 
                                        headers=headers, 
                                        data=data)

        #print 'URL',self.response.url
        #print 'data',data
        #print 'headers',headers
        #print vars(self.response)

    def read(self):
        return self.response.text

class SocketInterface(InterfaceBase):
    
    def __init__(self,addr,**args):
        self.host,self.port = addr.split(':')
        super(SocketInterface,self).__init__(addr,**args) 

        self.sock = None

    def enter(self):
        self.sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.settimeout(5)

    def exit(self):
        if self.sock:
            self.sock.close()

    def write(self,msg):
        self.sock.sendto(msg+' ',(self.host,self.port))

    def read(self):
        data,server = self.sock.recvfrom(1024)
        return data

class SerialInterface(InterfaceBase):

    def __init__(self,addr,**args):
        super(SerialInterface,self).__init__(addr,**args) 
        self.device = addr 
        self.port = None
    
        basename = os.path.basename(self.device)
        self.lockfile = lockfile.LockFile('/var/lock/LCK..%s' % basename)

    def enter(self):
        #self.port = serial.Serial(self.device,115200,timeout=5)
        self.port = serial.Serial(self.device,19200,timeout=5)
        if not self.lockfile.acquire():
            raise IOError('Failed to acquire lock')

    def exit(self):
        if self.port:
            self.port.close()
            self.lockfile.release()

    def write(self,msg):
        self.port.write(msg+'\r')

    def read(self):

        response = ''

        while len(response)<1024:

            #ch = self.port.read()
            #if not ch:
            #    break
            #response += ch

            line = self.port.readline()
            if not line:
                break
            response += line

            if response.endswith('*\r\n'):
                break

        if not response:
            raise Timeout
        return response

class PDUBase(object):

    def __init__(self,interface,model,log=None,**args):

        self.interface = interface
        self.model = model

        self.setDataMap({
            'RH':   self.RH,
            'RP':   self.RP,
            'RS':   self.RS,
            'RT':   self.RT,
            'RLTC': self.RLTC
            })

        if log is None:
            import logging
            logging.basicConfig(level=logging.INFO)
            self.log=logging
        else:
            self.log=log

    def __enter__(self):
        self.clearData()
        self.interface.enter()
        return self

    def __exit__(self,*exc_info):
        if exc_info[0]:
            import traceback
            traceback.print_exception(*exc_info)
        self.interface.exit()

    def currentTime(self):
        return datetime.datetime.utcnow().replace(tzinfo=UTC)

    def setDataMap(self,map):
        self.dataMap = map

    def addDataMap(self,label,func):
        self.dataMap[label] = func

    def delDataMap(self,label):
        if label in self.dataMap:
            del self.dataMap[label]

    def sendCommand(self,cmd):

        try:
            self.interface.write(cmd)
            data = self.interface.read()
        except socket.timeout:
            self.log.error('Timeout')
            raise

        data = data.replace('nan','-99')

        return data

    def setDataMap(self,map):
        self.dataMap = map

    def clearData(self):
        self.data = {}

        self.data['meta'] = self.interface.info() 

        for section in self.dataMap:
            self.data[section] = []

    def getData(self):
        return self.data

    def updateData(self):

        for func in self.dataMap.values():
            func()

        now = self.currentTime()
        unixtime = time.mktime(now.timetuple()) 


        self.data['meta']['timestamp'] = unixtime 
        self.data['meta']['model'] = self.model

        return self.data

    def readResults(self,cmd,func,reverse=True):
        result = self.sendCommand(cmd)
        values = [func(x) for x in parseFloats(result)]

        if reverse:
            values = list(reversed(values))

        self.data[cmd] = values

        return values

    def readSwitch(self,cmd,**args):
        return self.readResults(cmd,int,**args)

    def readFloat(self,cmd,**args):
        return self.readResults(cmd,float,**args)

    def RH(self):
        return self.readSwitch('RH')

    def RP(self):
        return self.readSwitch('RP')

    def RS(self):
        return self.readSwitch('RS')

    def RTS(self):
        return self.sendCommand('RTS')

    def RBME(self):
        results = self.readFloat('RBME',reverse=False)

        if not results: 
            results = [-999.0]*4
            
        self.data['RBME_dict'] = dict(
            temperature=results[0],
            pressure=results[1],
            altitude=results[2],
            humidity=results[3])

        return self.data['RBME']

    def RT(self):
        return self.readFloat('RT')

    def RLTC(self):
        result = self.sendCommand('RLTC')
        m = re.findall('(\d .+ .+)',result,re.I|re.M)
        volts = []
        amps = []
        for entry in m:
            index,volt,amp = entry.split()
            volts.append(float(volt))
            amps.append(float(amp))

        self.data['RLTC'] = dict(volts=volts,amps=amps)

        return self.data['RLTC']

    def LD(self,state):
        return self.sendCommand('LD %s' % state)

    def SA(self,state):
        return self.sendCommand('SA %s' % state)

    def ST(self,channel,low,high,zone):
        return self.sendCommand('ST %s %.1f %.1f %d' % (channel,low,high,zone))

    def SP(self,fet,state):
        return self.sendCommand('SP %s %s' % (fet,state))

    def status(self):
        return self.updateData()

class PDU_v2(PDUBase):
    
    # Harbinger V2 power control unit

    def __init__(self,addr=None,**args):
        interface = SocketInterface(addr)
        super(PDUv2,self).__init__(interface,'v2',**args)

        self.addDataMap('RMBE',self.RBME)

    def RBME(self):
        results = self.readFloat('RBME',reverse=False)
        self.data['RBME_dict'] = dict(
            temperature=results[0],
            pressure=results[1],
            altitude=results[2],
            humidity=results[3])

        return self.data['RBME']

class PDU_sp(PDUBase):

    # Harbinger small payload power control unit

    def __init__(self,addr=None,**args):
        interface = SerialInterface(addr)
        super(PDUsp,self).__init__(interface,'sp',**args)

        self.addDataMap('UP',self.UP)

    def VER(self):
        return self.sendCommand('VER')

    def UP(self):
        line = self.sendCommand('UP').split('*')[0].strip()

        try:
            days,hours,mins,secs = line.split(':')[1].split(',')

            days = int(days.strip().split(' ')[0])
            hours = int(hours.strip().split(' ')[0])
            mins = int(mins.strip().split(' ')[0])
            secs = int(secs.strip().split(' ')[0])

            uptime = secs + (mins + (hours + days*24)*60)*60
        except:
            uptime = 0

        self.data['UP'] = dict(raw=line,seconds=uptime)

        return line 

class PDU_webpowerswitch(PDUBase):

    # Digital Logger web power switch 
    #
    # We support both the newer REST API found on the Pro models
    # as well as the legacy API on the LPC 1-7 models. Because
    # we have a mix of these in the field, we test for the protocol
    # to use at startup. 

    def __init__(self,addr=None,auth=None,scheme='http',**args):
        interface = HTTPInterface(addr,auth=None,scheme=scheme)
        super(PDU_webpowerswitch,self).__init__(interface,'webpowerswitch',**args)

        self.username, self.password = auth.split(':')

        self.protocol = None

        self.setDataMap({
            'RS':   self.RS
            })

        self.protocol_rs_map = {
            'rest':     self.RS_rest,
            'legacy':   self.RS_legacy,
            'ancient':  self.RS_ancient
            }

        self.protocol_sp_map = {
            'rest':     self.SP_rest,
            'legacy':   self.SP_legacy,
            'ancient':  self.SP_legacy
            } 

    def sendCommand(self,cmd, method='GET', data=None, headers=None):

        try:
            self.interface.write(cmd, method=method, data=data, headers=headers)
            data = self.interface.read()
        except socket.timeout:
            self.log.error('Timeout')
            raise

        return data

    def check_protocol(self):
        """Return true if we figured out the protocol"""

        if self.protocol is not None:
            # We already know the protocol from an earlier test
            return True
    
        self.interface.auth = HTTPDigestAuth(self.username, self.password)

        response = self.sendCommand('/restapi/relay/outlets')

        try:
            json.loads(response)
            self.protocol = 'rest'
        except ValueError:
            self.protocol = 'legacy'
            auth = HTTPBasicAuth(self.username, self.password)
            self.interface.auth = auth

        return True

    def RS_rest(self):

        page = self.sendCommand('restapi/relay/outlets/')

        data = json.loads(page)

        values = ['on' if entry['state'] else 'off' for entry in data]

        return values
        
    def RS_ancient(self):

        page = self.sendCommand('index.htm').replace('\n','')

        outlets = {} 
        values = []

        pattern = 'center>(\d)</td><td>([^<]+)</td>\S+<font \S+>(\S+)</font>'

        for outlet,name,state in re.findall(pattern, page):
            outlets[int(outlet)] = state.lower()
        
        for outlet in range(1,9):
            values.append(outlets[outlet])

        return values

    def RS_legacy(self):

        page = self.sendCommand('status')

        values = []

        pattern = '<div id="state">(.+?)</div>'

        state = int(re.findall(pattern, page)[0],16)

        for k in range(8): 
            if state & 2**k:
                values.append('on')
            else:
                values.append('off')
        
        return values

    def RS(self):
        self.check_protocol()
        self.data['RS'] = self.protocol_rs_map[self.protocol]()
        return self.data['RS'] 

    def SP_rest(self,fet,state):
        endpoint = '/restapi/relay/outlets/%d/state/' % (int(fet)-1)
        data = {'value': str(state=='on').lower()} 
        headers = {'X-CSRF': 'x'}
        return self.sendCommand(endpoint, data=data, method='PUT', headers=headers)

    def SP_legacy(self,fet,state):
        endpoint = 'outlet'
        data = {str(fet): state.upper()}
        return self.sendCommand(endpoint, data=data)

    def SP(self, *pos, **kw):
        self.check_protocol()
        return self.protocol_sp_map[self.protocol](*pos, **kw)

def PDU(model=None,*pos,**args):
    
    if model=='v2':
        return PDU_v2(*pos,**args)
    elif model=='sp':
        return PDU_sp(*pos,**args)
    elif model=='webpowerswitch':
        return PDU_webpowerswitch(*pos,**args)

    raise ValueError('Unknown PDU type: %s' % model)

if __name__ == '__main__':

    import time
    
    #pdu_sim = PDU(model='v2',addr='127.0.0.1:8880')
    #pdu_v2  = PDU(model='v2',addr='10.51.10.81:8880')
    #pdu_sp  = PDU(model='sp',addr='/dev/ttyZ1')
    #pdu_wps  = PDU(model='webpowerswitch',addr='192.168.0.100',auth='transport:mangonet0')
    pdu_wps  = PDU(model='webpowerswitch',addr='192.168.0.100',auth='admin:1234')

    with pdu_wps as pdu: 
        pdu.SP(3,'on')
        print pdu.updateData()
        time.sleep(2)
        pdu.SP(3,'off')
        print pdu.updateData()

