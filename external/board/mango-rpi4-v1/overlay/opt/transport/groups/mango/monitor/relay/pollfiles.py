#!/usr/bin/env python2

############################################################################
#
#   Poll newsgroups for new files 
#
#   A modern take (although much less capable) version of ArchiveGroups
#
#   2021-07-15  Todd Valentic
#               Initial implementation. Based on postfiles.py 
#
############################################################################

from Transport  import ProcessClient
from Transport  import NewsTool
from Transport  import NewsPollMixin
from Transport  import ConfigComponent
from Transport.Util import removeFile

import os
import bz2
import sys
import json

class FileGroup(ConfigComponent, NewsPollMixin):

    def __init__(self, name, parent):
        ConfigComponent.__init__(self, 'filegroup', name, parent)
        NewsPollMixin.__init__(self, callback=self.process)

        self.outputpath = self.get('output.path','.')
        self.uncompress = self.getboolean('uncompress',False)

    def uncompressFile(self, pathname):

        # Only handle bzip2 files at this time

        self.log.info('Uncompressing %s' % pathname)

        with open(pathname,'rb') as f:
            data = f.read()

        output_pathname = os.path.splitext(pathname)[0]

        with open(output_pathname,'wb') as f:
            f.write(bz2.decompress(data))
            
        removeFile(pathname)

    def process(self, message):

        if not self.preprocess(message):
            return

        pathnames = NewsTool.saveFiles(message, path=self.outputpath)

        for pathname in pathnames:
            self.log.info('Saved %s' % pathname) 
            if self.uncompress and pathname.endswith('bz2'):
                self.uncompressFile(pathname)

        self.postprocess(message, pathnames)

    def preprocess(self, message):
        """Used by derived classes"""
        return True

    def postprocess(self, message, pathnames):
        """Used by derived classes"""
        return

class AckFileGroup(FileGroup):

    def __init__(self, *pos, **kw):
        FileGroup.__init__(self, *pos, **kw)

        ackpath = self.get('ack.path','.')
        ackname = self.get('ack.name','ack-%Y%m%d-%H%M%S.dat')

        self.ack_pathname = os.path.join(ackpath, ackname)

    def postprocess(self, message, pathnames):

        self.wait(1) # Ensure the time-based ack filenames are unique

        now = self.currentTime()
        serialnum = message['X-Transport-SerialNum']

        msg = dict(timestamp = now, 
                pathnames = pathnames, 
                serialnum = serialnum, 
                filegroup = self.name
                ) 

        ack_pathname = now.strftime(self.ack_pathname)
        ack_path = os.path.dirname(ack_pathname)

        if not os.path.exists(ack_path):
            os.makedirs(ack_path)

        with open(ack_pathname,'w') as f: 
            f.write(json.dumps(msg, default=str))

        self.log.info('  - serial num: %s' % serialnum)
        self.log.info('  - sent ack: %s' % os.path.basename(ack_pathname))

class PollFiles(ProcessClient):

    def __init__(self,argv, factory=FileGroup):
        ProcessClient.__init__(self,argv)

        self.pollrate = self.getRate('pollrate','5:00')
        self.exitOnError = self.getboolean('exitOnError', False)
        self.filegroups = self.getComponentsDict('filegroups', factory)

        self.log.info('Loaded %d file groups:' % len(self.filegroups))

        for filegroup in self.filegroups:
            self.log.info('  - %s' % filegroup)

    def process(self):

        self.log.debug('Processing feeds')

        for filegroup in self.filegroups.values():

            if not self.running:
                break

            try:
                filegroup.run_step()
            except StopIteration:
                break
            except SystemExit:
                self.running = False
                break
            except:
                self.log.exception('Error processing filegroup %s' % filegroup.name)
                if self.exitOnError:   
                    self.running = False 

    def run(self):

        while self.wait(self.pollrate):
            self.process()
    
        self.log.info('Finished')
 
if __name__ == '__main__':
    PollFiles(sys.argv, factory=AckFileGroup).run()
     
