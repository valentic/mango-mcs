#!/usr/bin/env python

# Listen with nc -u 127.0.0.1 8880

import socket

class UDPServer(object):

    def __init__(self,host,port):
        self._host = host
        self._port = port

    def __enter__(self):
        sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        sock.bind((self._host,self._port))
        self._sock = sock
        return sock

    def __exit__(self,*exc_info):
        if exc_info[0]:
            import traceback
            traceback.print_exception(*exc_info)
        self._sock.close()

def ReadHeater():
    return 'RH 0 0 0 0 0 0 0 0 *'

def ReadPower():
    return 'RP 0 0 0 0 0 0 0 0 0 0 0 *'

def ReadRails():
    return 'RS 0 0 0 0 0 0 0 0 0 0 0 *'

def ReadTemps():
    return 'RT 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 *'

def ReadBME():
    return 'RBME 25.40 1019.33 -50.48 37.39 *'

def ReadLTC():
    return """RLTC
1 102.4 0.0
2 102.4 0.0
3 102.4 0.0
4 102.4 0.0
5 102.4 0.0
6 102.4 0.0
7 102.4 0.0
8 102.4 0.0
9 102.4 0.0
10 102.4 0.0
11 102.4 0.0
12 102.4 0.0
*"""

def ReadTS():
    return """RTS
1 -40.0 -30.0 3
2 0.0 0.0 0
3 0.0 0.0 0
4 0.0 0.0 0
5 0.0 0.0 0
6 0.0 0.0 0
7 0.0 0.0 0
8 0.0 0.0 0
9 0.0 0.0 0
10 0.0 0.0 0
11 0.0 0.0 0
12 0.0 0.0 0
13 0.0 0.0 0
14 0.0 0.0 0
15 0.0 0.0 0
16 0.0 0.0 0
17 0.0 0.0 0
18 0.0 0.0 0
19 0.0 0.0 0
20 0.0 -35.0 2
*"""

handler = {
    'RH':   ReadHeater(),
    'RP':   ReadPower(),
    'RS':   ReadRails(),
    'RT':   ReadTemps(),
    'RBME': ReadBME(),
    'RLTC': ReadLTC(),
    'RTS':  ReadTS()
}

if __name__ == '__main__':

    host = '127.0.0.1'
    port = 8880

    with UDPServer(host,port) as s:
        while True:
            cmd,addr = s.recvfrom(1024)

            print 'cmd: %s, addr: %s' % (cmd,addr)

            try:
                msg = handler[cmd.strip()]
            except:
                msg = 'Error'

            s.sendto(msg,addr)
