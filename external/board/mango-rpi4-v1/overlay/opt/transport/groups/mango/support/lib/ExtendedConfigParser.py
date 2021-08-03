#!/usr/bin/env python

#####################################################################
#
#   Extended Configuration Parser
#
#   This is an extended version of the standard ConfigParser object
#   that is modeled after the Transport DefaultConfigParser. It
#   adds the ability to set default values and parse date/time
#   values.
#
#   2010-01-22  Todd Valentic
#               Initial implementation.
#
#   2011-04-02  Todd Valentic
#               Moved getTimeSpan from schedule.py to here
#
#   2011-08-25  Todd Valentic
#               Fixed parsing error in getTimeSpan
#               Add timedelta formatting to getTimeSpan
#
#   2012-02-15  Todd Valentic
#               Add getList and getSet
#
#   2021-05-24  Todd Valentic
#               Added getBytes 
#
#####################################################################

from ConfigParser import SafeConfigParser
from Transport.Util import datefunc
from Transport.Util.dateutil import parser
from Transport.Util.dateutil.relativedelta import relativedelta

import datetime

class ExtendedConfigParser(SafeConfigParser):

    def get(self,section,option,default=None,**kw):
        try:
            result = SafeConfigParser.get(self,section,option,**kw)
            if result=='None':
                return None
            else:
                return result
        except:
            return default

    def getint(self,section,option,default=None,**kw):
        try:
            return SafeConfigParser.getint(self,section,option,**kw)
        except:
            return default

    def getfloat(self,section,option,default=None,**kw):
        try:
            return SafeConfigParser.getfloat(self,section,option,**kw)
        except:
            return default

    def getboolean(self,section,option,default=None,**kw):
        try:
            return SafeConfigParser.getboolean(self,section,option,**kw)
        except:
            return default

    def getDateTime(self,section,key,default=None,**kw):

        datestr = self.get(section,key,**kw)

        if datestr is None:
            return default

        try:
            return parser.parse(datestr)
        except:
            return default

    def getDeltaTime(self,section,key,default=None,**kw):

        # Convert default into a timedelta object (it is usually
        # given as either a string (ie '00:23') or an int (secs).

        if not (default is None or isinstance(default,datetime.timedelta)):
            default = datefunc.parse_timedelta(str(default))

        try:
            value = self.get(section,key,**kw)
            return datefunc.parse_timedelta(value)
        except:
            return default

    def getTimeSpan(self,section,key,default=None,**kw):

        spanargs = self.get(section,key,default,**kw)

        if not spanargs:
            return None

        args = {}

        if '=' in spanargs:

            for arg in spanargs.split(','):
                key,value = arg.split('=')
                args[key.strip()] = int(value)

        else:

            td = datefunc.parse_timedelta(spanargs)
            args['days'] = td.days
            args['seconds'] = td.seconds

        return relativedelta(**args)

    def getList(self,section,key,default='',token=None):
        return self.get(section,key,default).split(token)

    def getSet(self,*pos,**kw):
        return set(self.getList(*pos,**kw))

    def getBytes(self,section,key,default=None,**kw):

        # Format: key=value [b|Kb|Mb|Gb] (ignores case)

        value = self.get(section,key,default,**kw)

        if value is None:
            return None

        if isinstance(value,int):
            return value

        if isinstance(value,float):
            return int(value)

        value = value.lower()

        try:
            if value[-2:]=='kb':
                return int(value[:-2])*1024
            elif value[-2:]=='mb':
                return int(value[:-2])*1024*1024
            elif value[-2:]=='gb':
                return int(value[:-2])*1024*1024*1024
            elif value[-1:]=='b':
                return int(value[:-1])
            else:
                return int(value)
        except:
            self.log.exception('Error parsing bytes for %s: %s' % (key,value))
            return 0

