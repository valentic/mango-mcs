#!/usr/bin/env python2

###################################################################
#
#   Location service
#
#   This service provides the best estimate for the station 
#   location using a variety of input sources (GPS, Iridium, etc).
#   The "best" one is the newest one in the priority order
#   having a valid position fix.
#
#   The output is a dictionary with the following fields:
#
#       latitude
#       longitude
#       src
#
#   2021-06-29  Todd Valentic
#               Initial implementation. 
#
###################################################################

from Transport  import ProcessClient
from Transport  import XMLRPCServerMixin
from Transport  import DirectoryMixin

import sys
import json

class Server(ProcessClient,XMLRPCServerMixin):

    def __init__(self,args):
        ProcessClient.__init__(self,args)
        XMLRPCServerMixin.__init__(self)

        self.register_function(self.best)

        self.cache = self.connect('cache')
        self.sources = self.getList('sources')

    def best(self):
        
        cacheEntries = self.cache.list()

        entry = None
        result = None 
       
        for source in self.sources:
            if source not in cacheEntries:
                continue

            entry = self.cache.get(source)

            if source=='gps' and entry['mode']<2: 
                # no position fix
                continue

            if 'latitude' not in entry:
                continue

            if 'longitude' not in entry:    
                continue
    
            result = {}

            result['src'] = source
            result['latitude'] = entry['latitude']
            result['longitude'] = entry['longitude']

            break

        return result 






        


if __name__ == '__main__':
    Server(sys.argv).run()

