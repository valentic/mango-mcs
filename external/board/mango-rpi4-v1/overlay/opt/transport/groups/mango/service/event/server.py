#!/usr/bin/env python2

################################################################
#
#   Event Notification Service
#
#   This service implements the observer pattern for
#   distributing events between clients.
#
#   See: http://en.wikipedia.org/wiki/Observer_pattern
#
#   2006-10-29  Todd Valentic
#               Initial implementation.
#
#   2007-04-16  Todd Valentic
#               Fixed bug in unregister if event not registered.
#
################################################################

from Transport                  import ProcessClient
from Transport                  import XMLRPCServerMixin
from Transport                  import AccessMixin
from threading                  import Thread

import  os
import  sys
import  Queue
import  socket
import  xmlrpclib

socket.setdefaulttimeout(5)

class WorkerThread(Thread,AccessMixin):

    def __init__(self,parent):
        Thread.__init__(self)
        AccessMixin.__init__(self,parent)

        self.queue = self.parent.queue

    def run(self):

        self.log.info('Worker thread starting')

        while self.running:

            try:
                event,signature,args = self.queue.get(timeout=1)
            except Queue.Empty:
                continue

            url,method = signature

            try:
                client = xmlrpclib.ServerProxy(url)
                eval('client.%s(*args)' % method)
            except:
                self.log.exception('Problem notifying %s for %s (%s)' % \
                    (signature,event,args))

        self.log.info('Worker thread exiting')

class Server(ProcessClient,XMLRPCServerMixin):

    def __init__(self,argv):
        ProcessClient.__init__(self,argv)
        XMLRPCServerMixin.__init__(self)

        self.register_function(self.register)
        self.register_function(self.unregister)
        self.register_function(self.notify)

        self.register_function(self.listObservers)
        self.register_function(self.listEvents)
        self.register_function(self.removeEvent)

        # Private methods used for testing

        self.register_function(self.testport)

        self.loadEvents()

        self.queue  = Queue.Queue()
        self.thread = WorkerThread(self)
        self.thread.start()

    def loadEvents(self):

        self.events = {}

        if not os.path.exists('events'):
            return

        for line in open('events'):
            event,url,method = line.split()
            self.addObserver(event,url,method)

    def saveEvents(self):

        output = open('events','w')

        for event in self.events:
            for url,method in self.events[event]:
                print >> output,event,url,method

        output.close()

    def removeEvent(self,event):
        if not event in self.events:
            return

        self.log.info('Removing event: %s' % event)

        del self.events[events]
        self.saveEvents()

    def register(self,event,url,method):
        self.addObserver(event,url,method)
        self.saveEvents()
        return 1

    def addObserver(self,event,url,method):
        if event not in self.events:
            self.events[event] = []
        signature = (url,method)
        if signature not in self.events[event]:
            self.events[event].append(signature)

    def unregister(self,event,url,method):
        signature = (url,method)

        if event in self.events:
            try:
                self.events[event].remove(signature)
            except:
                pass

            if self.events[event]==[]:
                del self.events[event]

        self.saveEvents()
        return 1

    def listEvents(self):
        return self.events.keys()

    def listObservers(self,event):
        return self.events[event]

    def notify(self,event,*args):

        self.log.info('Sending event %s' % event)

        if event not in self.events:
            self.log.info('  - no observers registered')
            return 1

        for signature in self.events[event]:
            self.queue.put((event,signature,args))

        return 1

    def testport(self,msg):
        self.log.info('Testport: msg=%s' % msg)
        return 1

    def run(self):
        XMLRPCServerMixin.run(self)

        # Need to wait for worker threads

        self.thread.join()

        self.log.info('Finished')

if __name__ == '__main__':
    Server(sys.argv).run()

