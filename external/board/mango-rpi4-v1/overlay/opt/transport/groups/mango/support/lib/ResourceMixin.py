#!/usr/bin/env python

##################################################################
#
#   Resource manager mixin class. 
#
#   Assumes that the base class has DirectoryMixin
#
#   You need to define instrument.name in the config file.
#
#   2010-03-17  Todd Valentic
#               Initial implementation. Split off from DataMonitor. 
#
##################################################################

from Transport import ProcessClient
from Transport import DirectoryMixin

import os
import stat
import time
import struct
import glob
import bz2
import StringIO
import ConfigParser
import datetime

class ResourceMixin:

    def __init__(self):

        self.instrument         = self.get('instrument.name')
        self.resourceManager    = self.connect('resources')
        self.resources          = {}

        if not self.instrument:
            self.abort('No instrument.name defined')

        self.resources = {}

    def allocate(self):
        self.log.info('allocating resources: %s' % self.resources.values())
        self.resourceManager.allocate(self.instrument,self.resources.values())

    def setResources(self,*args):

        self.resources = {}

        for resource in args:
            name = resource.split('=')[0]
            self.resources[name] = resource

        self.allocate()

    def clearResources(self):

        self.resources = {}
        self.allocate()

    def addResource(self,*args):

        for resource in args:
            name = resource.split('=')[0]
            self.resources[name] = resource

        self.allocate()

    def freeResource(self,*args):

        for resource in args:
            name = resource.split('=')[0]
            if name in self.resources:
                del self.resources[name]

        self.allocate()


