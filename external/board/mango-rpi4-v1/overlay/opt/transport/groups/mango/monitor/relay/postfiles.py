#!/usr/bin/env python2

############################################################################
#
#   Post new files 
#
#   2019-06-27  Todd Valentic
#               Initial implementation. Based on PostDataFiles.py
#
#   2019-07-23  Todd Valentic
#               Filter out names starting with '.' (these are partial files)
#
#   2019-08-03  Todd Valentic
#               Add time parsing for vmti names
#
#   2021-07-16  Todd Valentic
#               Make filename time parsing optional
#
#   2021-07-17  Todd Valentic
#               Add serialNum option
#               Use path, filename, pathname nomenclature
#
############################################################################

from Transport      import ProcessClient
from Transport      import NewsPostMixin
from Transport      import ConfigComponent
from Transport.Util import PatternTemplate, removeFile, sizeDesc
from dateutil       import parser

import re
import os
import bz2
import sys
import time
import commands
import pytz
import datetime
import uuid

class FileGroup(ConfigComponent, NewsPostMixin):

    def __init__(self,name,parent):
        ConfigComponent.__init__(self, 'filegroup', name, parent)
        NewsPostMixin.__init__(self, name=None)

        self.startPath      = self.get('start.path','.')
        self.matchPaths     = self.getList('match.paths','*')
        self.matchNames     = self.getList('match.names','*')
        self.compress       = self.getboolean('compress',False)
        self.removeFiles    = self.getboolean('removeFiles',False)
        self.includeLast    = self.getboolean('includeLast',True)
        self.startCurrent   = self.getboolean('startCurrent',False)
        self.maxFiles       = self.getint('maxFiles')
        self.enableParseTime = self.getboolean('parseTime',True)
        self.enableSerialNum = self.getboolean('serialNum',False)

        self.pathRule       = PatternTemplate('path','/')

        self.newsgroupTemplate = self.get('post.newsgroup.template')

        self.posters        = {}
        self.timeFilename   = '%s.timestamp' % name

        if not os.path.isfile(self.timeFilename):
            # Default to sometime long ago
            open(self.timeFilename,'w').write('0')
            t = time.mktime((1975,1,1,0,0,0,0,0,0))
            os.utime(self.timeFilename,(t,t))

        if self.startCurrent:
            os.utime(self.timeFilename,None)

        self.log.info('Watching for files in %s' % self.startPath)
        self.log.info('   - match paths %s' % ' '.join(self.matchPaths))
        self.log.info('   - match names %s' % ' '.join(self.matchNames))

    def parseTime(self, filename):
            
        # Standard format - 20190802-200555C00SEQ01.ntf
        # Log format - hs-07-20190803-165804.log
        # VMTI format - vmti_08-02-2019_UTC20-04-55_0000_00.4607

        regexs = [
            ('\d{8}.\d{6}','%Y%m%d-%H%M%S'),
            ('\d{2}-\d{2}-\d{4}_UTC\d{2}-\d{2}-\d{2}','%m-%d-%Y-UTC%H-%M-%S')
            ]

        timestamp = None

        for regex,timefmt in regexs:
            try:
                timestr = re.findall(regex,filename)[0]
                timestr = timestr.replace('_','-')
                timestamp = datetime.datetime.strptime(timestr,timefmt) 
                timestamp = timestamp.replace(tzinfo=pytz.utc)
                break
            except:
                pass

        if timestamp is None:
            self.log.warn('Unable to parse timestamp from filename: %s' % filename)

        return timestamp
 
    def post(self, pathname):

        if self.enableParseTime:
            timestamp = self.parseTime(os.path.basename(pathname))
        else:
            timestamp = None

        newsgroup = self.pathRule(self.newsgroupTemplate,pathname)
        filesize = os.path.getsize(pathname)

        self.log.info('  - posting %s (%s) to %s' % (pathname,sizeDesc(filesize),newsgroup))

        if not newsgroup in self.posters:
            self.put('post.newsgroup',newsgroup)
            poster = self.createNewsPoster('post')
            self.posters[newsgroup] = poster

        headers = {}

        if self.enableSerialNum:
            headers['X-Transport-SerialNum'] = str(uuid.uuid4())

        self.posters[newsgroup].post([pathname], date=timestamp, headers=headers)

    def processFile(self,pathname):

        self.log.info('Processing %s' % pathname)

        bzipext         = '.bz2'
        basename        = os.path.basename(pathname)
        baseext         = os.path.splitext(basename)[1]
        isCompressed    = baseext==bzipext
        zipname         = basename+bzipext

        if self.compress and not isCompressed:

            self.log.debug('  - compressing file')
            data = open(pathname).read()
            open(zipname,'w').write(bz2.compress(data))

            orgsize = os.path.getsize(pathname)
            zipsize = os.path.getsize(zipname)

            if orgsize>0:
                zippct  = (zipsize/float(orgsize))*100
            else:
                zippct  = 0

            self.log.info('  - %s -> %s (%d%%)' % \
                (sizeDesc(orgsize),sizeDesc(zipsize),zippct))

            postfile = zipname

        else:

            postfile = pathname

        self.post(postfile)

        # Cleanup files

        removeFile(zipname)

        if self.removeFiles:
            removeFile(pathname)

    def joinList(self, name, parts):

        result = ['-%s "%s"' % (name, part) for part in parts] 
        result = ' -o '.join(result)
        result = '\( %s \)' % result

        return result

    def findFiles(self):

        names = self.joinList('name', self.matchNames)
        paths = self.joinList('path', self.matchPaths)

        cmd = 'find %s -newer %s -type f %s %s -print' % \
            (self.startPath, self.timeFilename, paths,names)

        status,output = commands.getstatusoutput(cmd)   # run command, get output

        self.log.debug('cmd=%s' % cmd)

        if status!=0:
            return []

        filelist = output.split()         # make list, break at \n
        filelist = [f for f in filelist if not os.path.basename(f).startswith('.')]
        filelist.sort()

        if not self.includeLast:
            filelist = filelist[0:-1]       # don't include the current file

        if self.maxFiles:                   # keep only the last N files

            if self.removeFiles:
                removeFiles(filelist[:-maxFiles])

            filelist = filelist[-self.maxFiles:]

        return filelist

    def process(self):

        pathnames = self.findFiles()
        self.log.debug('Polling - found %d new files.' % len(pathnames))

        for pathname in pathnames:

            if not self.running:
                break 

            timestamp=os.path.getmtime(pathname)
            self.processFile(pathname)
            os.utime(self.timeFilename,(timestamp,timestamp))

class PostFiles(ProcessClient):

    def __init__(self, argv):
        ProcessClient.__init__(self, argv)

        self.pollrate = self.getRate('pollrate', '5:00')
        self.exitOnError = self.getboolean('exitOnError', False)

        self.filegroups = self.getComponentsList('filegroups', FileGroup)

    def preprocess(self):
        return

    def postprocess(self):
        return

    def process(self):

        self.preprocess()

        for filegroup in self.filegroups:
            try:
                filegroup.process()
            except SystemExit:
                self.running = False
            except:
                self.log.error('Problem processing filegroup %s' % filegroup.name)
                if self.exitOnError:
                    self.running = False

            if not self.running:
                break

        self.postprocess()

    def run(self):

        while self.wait(self.pollrate):
            try:
                self.process()
            except SystemExit:
                break 
            except:
                self.log.exception('Problem detected')
                if self.exitOnError:
                    break 

        self.log.info('Exiting')

if __name__ == '__main__':
    PostFiles(sys.argv).run()

