#!/usr/bin/env

###################################################################
#
#   Access to the directory service outside of data transport.
#
#   2005-11-11  Todd Valentic
#               Initial implementation.
#
#   2007-03-23  Todd Valentic
#               Added default timeout.
#
#   2021-07-13  Todd Valentic
#               Add module connect() method
#
###################################################################

import xmlrpclib
import socket

socket.setdefaulttimeout(3)

class Directory:

    def __init__(self,host='localhost',port=8411):

        url = 'http://%s:%d' % (host,port)
        self.directory = xmlrpclib.ServerProxy(url)

    def connect(self,service_name):
        url = self.directory.get(service_name,'url')
        return xmlrpclib.ServerProxy(url)


def connect(service_name, *pos, **kw):

    directory = Directory(*pos, **kw)
    return directory.connect(service_name)

if __name__ == '__main__':

    # Example usage - connect to beamcode service:

    dir = Directory()
    beamcodes = dir.connect('beamcodes')

    print beamcodes.list()

