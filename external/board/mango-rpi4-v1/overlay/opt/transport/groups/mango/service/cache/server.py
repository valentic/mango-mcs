#!/usr/bin/env python2

###################################################################
#
#   Caching service
#
#   This service is used to cache values between clients.
#
#   2009-11-03  Todd Valentic
#               Initial implementation
#
#   2009-11-11  Todd Valentic
#               Incorporate config lookup (replaced the separate
#                   config service).
#
#   2018-06-08  Todd Valentic
#               Integrate with event service
#
#   2019-07-19  Todd Valentic
#               Add get_or_default 
#
#   2021-06-30  Todd Valentic
#               Add timeout and expire processing
#               Add clear
#
#   2021-07-21  Todd Valentic
#               Simplify timeouts to always be in secs
#
###################################################################

from Transport  import ProcessClient
from Transport  import XMLRPCServerMixin
from Transport.Util import datefunc

import sys
import datetime

class Server(ProcessClient,XMLRPCServerMixin):

    def __init__(self,args):
        ProcessClient.__init__(self,args)
        XMLRPCServerMixin.__init__(self,callback=self.expire)

        self.register_function(self.get_value,'get')
        self.register_function(self.get_value_default,'get_or_default')
        self.register_function(self.get_age,'get_age')
        self.register_function(self.put)
        self.register_function(self.list)
        self.register_function(self.lookup)
        self.register_function(self.clear_value,'clear')
        self.register_function(self.set_timeout,'set_timeout')
        self.register_function(self.get_timeout,'get_timeout')
        self.register_function(self.clear_timeout,'clear_timeout')

        self.event = self.connect('event')

        self.cache = {}
        self.timestamp = {}
        self.timeouts = {}           

    def expire(self):
        
        for key,timeout in self.timeouts.items():
            if key in self.cache:
                if self.get_age(key) > timeout:
                    self.clear_value(key)

    def set_timeout(self,key,secs):
        self.timeouts[key]=secs

    def clear_timeout(self,key):
        if key in self.timeouts:
            del self.timeouts[key]

    def get_timeout(self,key):
        return self.timeouts.get(key)

    def put(self,key,value):
        self.cache[key] = value
        self.timestamp[key] = self.currentTime() 
        self.event.notify(key,value)
        return True

    def get_value(self,key):
        return self.cache[key]

    def get_value_default(self,key,default):
        
        if key in self.cache:
            return self.cache[key]
        else:
            return default

    def get_age(self,key):
        age = self.currentTime() - self.timestamp[key]  
        return datefunc.timedelta_as_seconds(age)

    def clear_value(self,key):
        if key in self.cache:
            del self.cache[key]
            del self.timestamp[key]
            
        return True

    def list(self):
        return self.cache.keys()

    def lookup(self,keyword):
        return self.get(keyword)

if __name__ == '__main__':
    Server(sys.argv).run()

