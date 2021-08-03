// Compile with gcc evgpioctl.c -o evgpioctl evgpio.c -mcpu=arm9

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "evgpio.h"

void usage(char **argv) {
	fprintf(stderr,
		"Usage: %s [OPTIONS] ...\n"
		"EVGPIO utility\n"
		"\n"
		"  -i, --getin  <dio>    Returns the input value of a DIO\n"
		"  -s, --setout <dio>    Sets a DIO output value high\n"
		"  -l, --clrout <dio>    Sets a DIO output value low\n"
		"  -o, --ddrout <dio>    Set DIO to an output\n"
		"  -d, --ddrin <dio>     Set DIO to an input\n"
		"  -m, --setmask <dio>   Mask out a DIO so it does not provide input\n"
		"                        event changes and trigger the shared IRQ\n"
		"  -c, --clrmask <dio>   Clear the mask from a DIO so it provides input\n"
		"                        event changes and trigger the shared IRQ\n"
		"  -a, --maskall         Mask out all DIO so they do not provide input\n"
		"                        event changes and trigger the shared IRQ\n"
		"  -u, --unmaskall       Clear all masks so all DIO provide input event\n"
		"                        changes and trigger the shared IRQ\n"
		"  -w, --watch           Prints csv output when any unmasked DIO changes\n"
		"                        <dio>,<1=high,0=low>\n"
		"\n",
		argv[0]
	);
}

void print_csv(int dio, int value)
{
	printf("%d,%d\n", dio, value);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	int opt_watch = 0;
	int c, i;

	static struct option long_options[] = {
		{ "getin", 1, 0, 'i' },
		{ "setout", 1, 0, 's' },
		{ "clrout", 1, 0, 'l' },
		{ "ddrout", 1, 0, 'o' },
		{ "ddrin", 1, 0, 'd' },
		{ "setmask", 1, 0, 'm' },
		{ "clrmask", 1, 0, 'c' },
		{ "maskall", 0, 0, 'a' },
		{ "unmaskall", 0, 0, 'u' },
		{ "watch", 0, 0, 'w' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};
	evgpioinit();

	while((c = getopt_long(argc, argv, "i:s:l:o:d:m:c:whau", long_options, NULL)) != -1) {
		int dio;
		switch(c) {
		case 'i':
			dio = atoi(optarg);
			printf("dio%d=%d\n", dio, evgetin(dio));
			break;
		case 's':
			dio = atoi(optarg);
			evsetdata(dio, 1);
			break;
		case 'l':
			dio = atoi(optarg);
			evsetdata(dio, 0);
			break;
		case 'o':
			dio = atoi(optarg);
			evsetddr(dio, 1);
			break;
		case 'd':
			dio = atoi(optarg);
			evsetddr(dio, 0);
			break;
		case 'm':
			dio = atoi(optarg);
			evsetmask(dio, 1);
			break;
		case 'c':
			dio = atoi(optarg);
			evsetmask(dio, 0);
			break;
		case 'a':
			for (i = 0; i < 128; i++)
			{
				evsetmask(i, 1);
			}
			break;
		case 'u':
			for (i = 0; i < 128; i++)
			{
				evsetmask(i, 0);
			}
			break;
		case 'w':
			opt_watch = 1;
			break;
		case 'h':
			usage(argv);
			return 0;
		default:
			usage(argv);
			fprintf(stderr, "Unknown option\n");
			return 1;
		}
	}

	if(!opt_watch) return 0;

	evclrwatch();
	while(1) {
		evwatchin(print_csv);
	}
}
