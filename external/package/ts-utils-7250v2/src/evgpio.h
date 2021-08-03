#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef _EVGPIO_H_
#define _EVGPIO_H_

// Run once to map memory and set model
void evgpioinit();

// Set data output value, 1 high, 0 low
void evsetdata(int dio, int value);

// Set DIO direction, 1 output, 0 input
void evsetddr(int dio, int value);

// Set or clear the watch mask for a dio
void evsetmask(int dio, int value);

// Returns the input value for a dio
int evgetin(int dio);

// Clears all pending events
void evclrwatch();

// Get a callback on DIO change.  
// arg1 "dio" is the dio that changed
// arg2 "value" indicates how it changed
//// 1 == went from low to high
//// 0 == went form high to low
void evwatchin(void (*evcb)(int dio, int value));

#endif // _EVGPIO_H_