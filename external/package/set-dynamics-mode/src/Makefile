.PHONY: all install clean

all: set_dynamics_mode

set_dynamics_mode: set_dynamics_mode.c
	$(CC) -o set_dynamics_mode set_dynamics_mode.c

install:
	install -d /usr/local/bin
	install -m 755 set_dynamics_mode /usr/local/bin
	install -d /etc/gpsd 
	install -m 755 device-hook /etc/gpsd

clean:
	rm -f set_dynamics_mode


