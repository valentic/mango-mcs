#!/usr/bin/env python2

###########################################################
#
#   Collect and post a snapshot of the computer health:
#
#       - disk usage
#       - memory usage
#       - processor load
#       - uptime
#       - network usage
#
#   2007-09-12  Todd Valentic
#       Initial implementation. Based on the
#           ResourceMonitor component
#
#   2009-09-22  Todd Vaelntic
#       Relax constraint on /proc/mounts to include
#           file systems with no device (to pick up /tmp).
#
#   2021-05-24  Todd Valentic
#       Add MAC address for Ethernet devices
#
#   2021-08-15  Todd Valentic
#       Add temperature section
#
###########################################################

from datetime       import datetime

import os
import sys
import StringIO
import ConfigParser
import glob

class ResourceMonitor:

    def __init__(self,argv):

        self.lasttx = {}
        self.lastrx = {}

    def updateMounts(self,stats):

        mounts = open('/proc/mounts').read().split('\n')
        mounts = filter(lambda x: len(x),mounts)
        mounts = map(lambda x: x.split()[0:4],mounts)

        paths  = []

        for device,path,fstype,access in mounts:

            try:
                info = os.statvfs(path)
            except:
                continue

            if info.f_blocks==0:
                continue

            if stats.has_section(path):
                continue

            paths.append(path)

            totalbytes  = info.f_blocks*info.f_bsize
            freebytes   = info.f_bavail*info.f_bsize
            reserved    = info.f_bfree*info.f_bsize-freebytes
            totalavail  = totalbytes-reserved
            usedbytes   = totalavail-freebytes
            usedpct     = usedbytes/float(totalavail)*100

            stats.add_section(path)
            stats.set(path,'device',device)
            stats.set(path,'fstype',fstype)
            stats.set(path,'access',access)
            stats.set(path,'totalbytes',totalavail)
            stats.set(path,'freebytes',freebytes)
            stats.set(path,'usedbytes',usedbytes)
            stats.set(path,'usedpct',usedpct)

        stats.set('System','mounts',' '.join(paths))

    def updateMemory(self,stats):

        lines = open('/proc/meminfo').read().split('\n')

        if 'total:' in lines[0]:     # old style format
            lines=lines[3:]

        section = 'Memory'
        stats.add_section(section)

        for line in lines:
            try:
                key,value = line.split(':')
                value = int(value.split()[0])*1024
                stats.set(section,key,str(value))
            except:
                pass

    def updateLoad(self,stats):

        info = open('/proc/loadavg').read().split('\n')
        load = info[0].split()

        section = 'Load'

        stats.add_section(section)
        stats.set(section,'1min',load[0])
        stats.set(section,'5min',load[1])
        stats.set(section,'15min',load[2])

    def updateSwaps(self,stats):

        section = 'Swaps'
        stats.add_section(section)

        info = open('/proc/swaps').read().split('\n')

        swaps = []

        for line in info[1:-1]:
            try:
                dev,type,size,used,priority = line.split()
            except:
                continue
            swaps.append(dev)
            stats.set(section,dev+'.type',type)
            stats.set(section,dev+'.size',str(int(size)*1024))
            stats.set(section,dev+'.used',str(int(used)*1024))
            stats.set(section,dev+'.priority',priority)

        stats.set(section,'mounts',' '.join(swaps))

    def updateUptime(self,stats):

        info = open('/proc/uptime').read().split('\n')
        secs = float(info[0].split()[0])

        section = 'Uptime'
        stats.add_section(section)
        stats.set(section,'seconds',secs)

    def computeRate(self,prevbytes,prevtime,curbytes,curtime):
        if prevbytes>curbytes:
            # Counter rollover
            deltabytes = 2**32-prevbytes + curbytes
        else:
            deltabytes = curbytes-prevbytes
        return deltabytes/(curtime-prevtime).seconds

    def updateNetwork(self,stats):

        info = open('/proc/net/dev').read().split('\n')[2:-1]

        section = 'Network'
        stats.add_section(section)

        devs = []
        now  = datetime.now()

        for device in info:

            name,data   = device.split(':')
            name        = name.strip()
            data        = data.split()

            macaddr = open('/sys/class/net/%s/address' % name).read().strip()

            if self.lasttx.has_key(name):
                txbytes,txtime = self.lasttx[name]
                rxbytes,rxtime = self.lastrx[name]
                txrate = self.computeRate(txbytes,txtime,float(data[8]),now)
                rxrate = self.computeRate(rxbytes,rxtime,float(data[0]),now)
            else:
                txrate = 0
                rxrate = 0

            stats.set(section,name+'.macaddr',macaddr)
            stats.set(section,name+'.rx.rate',rxrate)
            stats.set(section,name+'.rx.bytes',data[0])
            stats.set(section,name+'.rx.packets',data[1])
            stats.set(section,name+'.rx.errs',data[2])
            stats.set(section,name+'.rx.drop',data[3])

            stats.set(section,name+'.tx.rate',txrate)
            stats.set(section,name+'.tx.bytes',data[8])
            stats.set(section,name+'.tx.packets',data[9])
            stats.set(section,name+'.tx.errs',data[10])
            stats.set(section,name+'.tx.drop',data[11])

            self.lasttx[name] = (float(data[8]),now)
            self.lastrx[name] = (float(data[0]),now)

            devs.append(name)

        stats.set(section,'devices',' '.join(devs))

    def updateTemps(self, stats):

        zones = glob.glob('/sys/class/thermal/thermal_zone*')
        
        section = 'Temperature'
        stats.add_section(section)

        for zone in sorted(zones):
            temp_c = open(os.path.join(zone,'temp')).read().strip()
            temp_c = float(temp_c)/1000
            stats.set(section, os.path.basename(zone), temp_c) 

    def status(self):

        timestamp = datetime.now()

        stats = ConfigParser.ConfigParser()
        stats.add_section('System')
        stats.set('System','timestamp',str(timestamp))

        self.updateMounts(stats)
        self.updateMemory(stats)
        self.updateLoad(stats)
        self.updateUptime(stats)
        self.updateNetwork(stats)
        self.updateSwaps(stats)
        self.updateTemps(stats)

        buffer = StringIO.StringIO()
        stats.write(buffer)

        return buffer.getvalue()

if __name__ == '__main__':
   monitor = ResourceMonitor(sys.argv)
   print monitor.status()

