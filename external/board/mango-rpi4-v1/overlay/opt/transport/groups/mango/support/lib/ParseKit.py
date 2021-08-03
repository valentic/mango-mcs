#!/usr/bin/env python

#####################################################################
#
#   Regular expression helpers
#
#   2018-05-30  Todd Valentic
#               Initial implementation
#
#####################################################################

import re

numeric_const_pattern = '[-+]? (?: (?: \d* \. \d+ ) | (?: \d+ \.? ) )(?: [Ee] [+-]? \d+ ) ?'

rx_float = re.compile(numeric_const_pattern,re.VERBOSE)

def parseFloat(msg):
    return float(rx_float.findall(msg)[0])

def parseFloats(msg):
    return [float(x) for x in rx_float.findall(msg)]

if __name__ == '__main__':
    print parseFloat('_ISIG=43.2')

