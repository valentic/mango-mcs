#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>

/*
Simple program that sets stationary mode in U-Blox GPS connected to serial 0 on a Raspberry Pi 3 running Jessie.  This
was not tested on any other system.  This program assumes the GPS is using its default 9600 baud, 8 data bits, 1 stop bit,
and no parity.  I'm using the expansion board from Uputronics.
This program worked for me when I set up my Pi as a stratum 1 NTP server per the directions on this 
web page: https://ava.upuaut.net/?p=726&cpage=1
Initially I had problems with the code that page points to in a section titled "Setting Stationary Mode".  That program
compiled just fine, but simply hung on the call to open().  As I started to troubleshoot that code, I realized it had more
problems than just the hanging, so I wrote this.
My apologies in advance for the lack of generalization and the generally bad code.  My only excuse is that this is the first
C program I've written in about 30 years (I've been working in Java for 20 years, and C++ before that)...
*/

struct ubxMsg {
	int size;
	unsigned char* msg;
};

unsigned char stationary[] = {
	0xB5, 0x62,             /* message header */ \
	0x06, 0x24,             /* message class and ID */  \
	0x24, 0x00,             /* message body length */  \
	0xFF, 0xFF,             /* parameters bitmask */ \
	0x08,                   /* dynamic platform model: high-altitude */ \
	0x03,                   /* position fixing mode: auto 2D/3D */ \
	0x00, 0x00, 0x00, 0x00, /* fixed altitude for 2D fix mode: 0 m */ \
	0x10, 0x27, 0x00, 0x00, /* fixed altitude variance for 2D fix mode: 1 m^2 */ \
	0x05,                   /* minimum elevation for GNSS satellite: 5 degrees */ \
	0x00,                   /* reserved */ \
	0xFA, 0x00,             /* position DOP mask: 0x00FA */ \
	0xFA, 0x00,             /* time DOP mask: 0x00FA */ \
	0x64, 0x00,             /* position accuracy mask: 100 m */ \
	0x5E, 0x01,             /* time accuracy mask: ??? m */ \
	0x00,                   /* static hold threshold: 0 cm/s */ \
	0x3C,                   /* DGNSS timeout: 60 seconds */ \
	0x00,                   /* number of satellites required above C/N0 threshold */ \
	0x00,                   /* C/N0 threshold: 0dBHz */ \
	0x00, 0x00,             /* reserved */ \
	0x00, 0x00,             /* static hold distance threshold: 0 m */ \
	0x00,                   /* UTC standard: auto */ \
	0x00, 0x00, 0x00, 0x00, /* reserved */ \
	0x00,                   /* reserved */ \
	0x86, 0x4C              /* checksum */ \
	};
unsigned char save[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x1D, 0xAB};


struct ubxMsg getStationaryMessage() {
    struct ubxMsg result;
    result.msg = stationary;
    result.size = sizeof( stationary );
    return result;
}


struct ubxMsg getSaveConfigurationMessage() {
	struct ubxMsg result;
	result.msg = save;
	result.size = sizeof( save );
	return result;
}


int openUART(const char* device) {

	int fd = -1;
	fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);		//Open in non blocking read/write mode

	if (fd == -1)
	{
		printf( "Can't open ttyS1 - possibly it's in use by another application\n" );
	}

	struct termios options;
	tcgetattr(fd, &options);
	options.c_cflag = B9600 | CS8 | CLOCAL | CREAD;
	options.c_iflag = IGNPAR;
	options.c_oflag = 0;
	options.c_lflag = 0;
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &options);

	return fd;
}



// returns character if available, otherwise -1
int readUART( int fd ) {

	// Read a character from the port, if there is one
	unsigned char rx_buffer[1];
	int rx_length = read( fd, (void*)rx_buffer, 1 );		//Filestream, buffer to store in, number of bytes to read (max)
	if (rx_length < 0)
	{
		if( rx_length < -1 ) {
			printf( "Read error, %d\n", rx_length );
			return -2;
		}
		return -1;
	}
	else if (rx_length == 0)
	{
		//No data waiting
		return -1;
	}
	else
	{
		//Bytes received
		return rx_buffer[0];
	}
}


// waits up to five seconds for the given string to be read
// returns 0 for failure, 1 for success
int waitForString( int fd, char* str, int size ) {
	int attempts = 0;
	int gotIt = 0;
	int index = 0;
	while( (attempts < 5000) && (gotIt == 0) ) {
		usleep( 1000 );
		int x = readUART( fd );
		if( x >= 0 ) {
			//printf("%c", x);
			if( x == str[index] ) {
				index++;
				if( index == size ) {
					gotIt = 1;
				}
			}
			else {
				index = 0;
			}
		}
		attempts++;
	}
	return gotIt;
}

// return 0 if error, 1 if succeeded
int sendUBXMessage( int fd, struct ubxMsg msg ) {

	// first we wait until our receiver is synchronized, by looking for a message
	// we know should be happening often ("$GNGGA,")
	// we'll wait up to five seconds for this
	if( waitForString( fd, "$GNGGA,", 7 ) == 0 )
		return 0;
	printf( "Synchronized...\n" );
	
	// then we blast the message out...
	//printf("message size: %d\n", msg.size);
	int c = write( fd, msg.msg, msg.size );
	
	// construct our expected acknowledge message and wait for it
	unsigned char expect[] = {0xb5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00};
	expect[6] = msg.msg[2];
	expect[7] = msg.msg[3];
	return waitForString( fd, expect, 8 );
} 


int main( int argc, char *argv[] ) {

	if (argc<2) {
	    printf("Need to specify device (i.e. /dev/ttyS1)\n");
	    exit(1);
	}

	const char* device = argv[1];

	int fd = openUART(device);
	if( fd < 0 )
		return 0;

	int result = sendUBXMessage( fd, getStationaryMessage() );
	if( result == 1 ) {
		printf( "Stationary mode successfully set...\n" );
		result = sendUBXMessage( fd, getSaveConfigurationMessage() );
		if( result == 1 ) {
			printf( "Configuration successfully saved...\n" );
		}
		else {
			printf( "Failed to save configuration!\n" );
		}
	}
	else {
		printf( "Failed to send stationary message!\n" );
	}
	close( fd );

	exit(0);
}
