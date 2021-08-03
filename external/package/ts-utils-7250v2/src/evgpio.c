#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#include "evgpio.h"

volatile uint16_t *syscon = 0;
uint16_t model;

inline void evgpioinit() {
	if(syscon == 0) {
		int mem = open("/dev/mem", O_RDWR|O_SYNC);
		syscon = mmap(0,
			getpagesize(),
			PROT_READ|PROT_WRITE,
			MAP_SHARED,
			mem,
			0x80004000);
	}
	model = syscon[0];
}

// Set data output value, 1 high, 0 low
void evsetdata(int dio, int value)
{
	if(model == 0x7700) {
		if(value) syscon[0x10/2] = (dio | 0x80);
		else syscon[0x10/2] = dio & ~0x80;
		return;
	}
	if(dio < 64) {
		if(value) syscon[0x36/2] = dio | 0x80;
		else syscon[0x36/2] = dio & ~0x80;
	} else {
		if(value) syscon[0x3a/2] = (dio - 64) | 0x80;
		else syscon[0x3a/2] = (dio - 64) & ~0x80;
	}
}

// Set DIO direction, 1 output, 0 input
void evsetddr(int dio, int value)
{
	if(model == 0x7700) {
		if(value) syscon[0x10/2] = dio | 0xc0;
		else syscon[0x10/2] = (dio | 0x40) & ~0x80;
		return;
	}
	if(dio < 64) {
		if(value) syscon[0x36/2] = dio | 0xc0;
		else syscon[0x36/2] = (dio | 0x40) & ~0x80;
	} else {
		if(value) syscon[0x3a/2] = (dio - 64) | 0xc0;
		else syscon[0x3a/2] = ((dio  - 64)| 0x40) & ~0x80;
	}
}

void evsetmask(int dio, int value)
{
	if(model == 0x7700) {
		if(value) syscon[0x14/2] = dio | 0x80;
		else syscon[0x14/2] = dio & ~0x80;
		return;
	}
	if(dio < 64) {
		if(value) syscon[0x38/2] = dio | 0x80;
		else syscon[0x38/2] = dio & ~0x80;
	} else {
		if(value) syscon[0x3c/2] = (dio - 64) | 0x80;
		else syscon[0x3c/2] = (dio - 64) & ~0x80;
	}
}

int evgetin(int dio)
{
	int value = 0;
	uint16_t x;
	if(model == 0x7700) {
		syscon[0x14/2] = dio | 0x80; // maskpoke
		while (syscon[0x10/2] & 0x100); // flush events	
		syscon[0x14/2] = dio & ~0x80; // maskpoke
		while ((x = syscon[0x10/2]) & 0x100)  
		  if ((x & 0x7f) == dio) value = (x & 0x80);
		return value ? 1 : 0;
	}

	if(dio < 63) {
		syscon[0x38/2] = dio | 0x80; // maskpoke
		while (syscon[0x36/2] & 0x100); // flush events	
		syscon[0x38/2] = dio & ~0x80; // maskpoke
		while ((x = syscon[0x36/2]) & 0x100)  
		  if ((x & 0x7f) == dio) value = (x & 0x80);
		return value ? 1 : 0;
	}

	syscon[0x3c/2] = (dio - 64) | 0x80; // maskpoke
	while (syscon[0x3a/2] & 0x100); // flush events	
	syscon[0x3c/2] = (dio - 64) & ~0x80; // maskpoke
	while ((x = syscon[0x3a/2]) & 0x100)  
	  if ((x & 0x7f) == (dio - 64)) value = (x & 0x80);
	return value ? 1 : 0;
}

void evclrwatch()
{
	uint16_t data;
	if(model == 0x7700) {
		do {
			data = syscon[0x10/2];
		} while ((data >> 8) & 0x1);
		return;
	}

	do {
		data = syscon[0x36/2];
	} while ((data >> 8) & 0x1);

	do {
		data = syscon[0x3a/2];
	} while ((data >> 8) & 0x1);
}

// Get a callback on DIO change.
void evwatchin(void (*evcb)(int dio, int value))
{
	int ret;
	int buf; // Holds irq junk data
	fd_set fds;
	assert(evcb != NULL);

	if(model == 0x7700) {
		int irqfd = open("/proc/irq/70/irq", O_RDONLY| O_NONBLOCK, S_IREAD);
		if(irqfd == -1) perror( "Failed to open irq");
		assert(irqfd != -1);
		FD_SET(irqfd, &fds); //add the fd to the set
		select(irqfd + 1, &fds, NULL, NULL, NULL);
		if(FD_ISSET(irqfd, &fds)) {
			uint16_t data;
			data = syscon[0x10/2];
			do {
				evcb(data & ~0x1c0, (data >> 7) & 0x1);
				data = syscon[0x10/2];
			} while ((data >> 8) & 0x1);
			// Clear the junk data in the IRQ file
			read(irqfd, &buf, sizeof(buf));
		}

		close(irqfd);
	} else {
		int irqfd1 = open("/proc/irq/78/irq", O_RDONLY| O_NONBLOCK, S_IREAD);
		if(irqfd1 == -1) perror( "Failed to open irq");
		int irqfd2 = open("/proc/irq/79/irq", O_RDONLY| O_NONBLOCK, S_IREAD);
		if(irqfd2 == -1) perror( "Failed to open irq");

		assert(irqfd1 != -1);
		assert(irqfd2 != -1);
		
		evclrwatch();
		
		do {
			int max;
			FD_ZERO(&fds);
			FD_SET(irqfd1, &fds);
			FD_SET(irqfd2, &fds); 
			if(irqfd1 > irqfd2) max = irqfd1;
			else max = irqfd2;
			ret = select(max + 1, &fds, NULL, NULL, NULL);
		} while (ret == -1 && errno == EINTR);
		
		if(FD_ISSET(irqfd1, &fds)) {
			uint16_t data;
			data = syscon[0x36/2];
			do {
				evcb(data & ~0x1c0, (data >> 7) & 0x1);
				data = syscon[0x36/2];
			} while ((data >> 8) & 0x1);
			// Clear the junk data in the IRQ file
			read(irqfd1, &buf, sizeof(buf));
		}

		if(FD_ISSET(irqfd2, &fds)) {
			uint16_t data;
			data = syscon[0x3a/2];
			do {
				evcb((data & ~0x1c0) + 64, (data >> 7) & 0x1);
				data = syscon[0x3a/2];
			} while ((data >> 8) & 0x1);
			// Clear the junk data in the IRQ file
			read(irqfd1, &buf, sizeof(buf));
		}

		close(irqfd1);
		close(irqfd2);
	}	
}
