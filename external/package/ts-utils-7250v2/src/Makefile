all: tshwctl xuartctl evgpioctl

#COPTS=-fno-tree-cselim -Wall -O -mcpu=arm9
COPTS=-fno-tree-cselim -O -mcpu=arm9

evgpioctl: evgpioctl.o evgpio.o 
	$(CC) $(COPTS) -mcpu=arm9 -o evgpioctl evgpioctl.o  evgpio.o

xuartctl: xuartctl.c xuartcore.c
	$(CC) $(COPTS) -mcpu=arm9 -o xuartctl xuartctl.c -lutil 

tshwctl: tshwctl.o evgpio.o mvdmastream.o ispvm.o
	$(CC) $(COPTS) -mcpu=arm9 -o tshwctl tshwctl.o evgpio.o mvdmastream.o ispvm.o

evgpioctl.o: evgpioctl.c
	$(CC) $(COPTS) -mcpu=arm9 -c evgpioctl.c

tshwctl.o: tshwctl.c evgpio.h mvdmastream.h
	$(CC) $(COPTS) -mcpu=arm9 -c tshwctl.c 

ispvm.o: ispvm.c vmopcode.h 
	$(CC) $(COPTS) -mcpu=arm9 -c ispvm.c 

evgpio.o: evgpio.c evgpio.h
	$(CC) $(COPTS) -mcpu=arm9 -c evgpio.c 
	
mvdmastream.o: mvdmastream.c mvdmastream.h
	$(CC) $(COPTS) -mcpu=arm9 -c mvdmastream.c 

clean:
	rm -f *.o
	rm -f tshwctl xuartctl evgpioctl

