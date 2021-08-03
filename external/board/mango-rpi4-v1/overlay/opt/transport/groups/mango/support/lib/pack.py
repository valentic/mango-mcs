#!/usr/bin/env python

#########################################################
#
#   Work around for 2.4 python struct module that does
#   handle packing NaN, +Inf and -Inf. The function
#   pack_one_safe() intercepts !d and !f float parameters
#   and writes out the correct value. Note that this
#   only handles those specific cases. This problem is
#   fixed in later python versions, so hopefully in the
#   future we can remove this hack.
#
#   2009-11-04  Todd Valentic
#               Initial implementation
#
#   2009-11-07  Todd Valentic
#               Added packFloat() and packDouble()
#
#########################################################

import struct

pack_double_workaround = {
  'inf': struct.pack('!Q', 0x7ff0000000000000L),
  '-inf': struct.pack('!Q', 0xfff0000000000000L),
  'nan': struct.pack('!Q', 0x7ff8000000000000L)
}

pack_float_workaround = {
    'inf': struct.pack('!I', 0x7f800000),
    '-inf': struct.pack('!I', 0xff800000),
    'nan': struct.pack('!I', 0x7fc00000)
}

def pack_one_safe(f, a):
    if f == '!d' and str(a) in pack_double_workaround:
        return pack_double_workaround[str(a)]
    if f == '!f' and str(a) in pack_float_workaround:
        return pack_float_workaround[str(a)]
    return struct.pack(f, a)

def packFloat(value):
    return pack_one_safe('!f',value)

def packDouble(value):
    return pack_one_safe('!d',value)

if __name__ == '__main__':
    print '!f 12.2 is',repr(pack_one_safe('!f',12.2))
    print 'I 23 is',repr(pack_one_safe('I',23))
    print '!f nan is',repr(pack_one_safe('!f',float('nan')))

