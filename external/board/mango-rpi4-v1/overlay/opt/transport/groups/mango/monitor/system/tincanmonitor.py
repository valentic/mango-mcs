#!/usr/bin/env python2

##################################################################
#
#   Monitor for tincan network config changes
#
#   Post latest version of the tincan configuration file to the
#   cache service.
#
#   2021-06-30  Todd Valentic
#               Initial implementation.
#
##################################################################

from DataMonitor import DataMonitorComponent

import os
import hashlib 
import json 

class TincanMonitor (DataMonitorComponent):

    def __init__(self,*pos,**kw):
        DataMonitorComponent.__init__(self,*pos,**kw)

        self.configFilename = self.get('tincan.configfile','/etc/tincan/data/config.json')

        self.checksum = None
        self.cacheKey = 'tincan'

    def loadConfig(self, filename):
        payload = open(filename).read() 
        data = json.loads(payload)
        checksum = hashlib.md5(payload).hexdigest()

        return data,checksum
        
    def sample(self):

        if not os.path.exists(self.configFilename):
            self.log.debug('No config file found: %s' % self.configFilename)
            return None

        try:
            data, checksum = self.loadConfig(self.configFilename)
        except:
            self.log.exception('Problem parsing the config file')
            return None

        if self.cacheKey not in self.listCache(): 
            self.checksum=None

        if checksum!=self.checksum:
            self.log.info('Detected modified config')
            self.putCache(self.cacheKey,data)
            self.checksum = checksum

        if self.cacheKey not in self.listCache(): 
            self.putCache(self.cacheKey,data)

        return None

