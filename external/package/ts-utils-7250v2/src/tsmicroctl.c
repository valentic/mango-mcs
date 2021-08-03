/* Compile with: gcc -mcpu=arm9 tsmicroctl.c -o tsmicroctl */

#include <stdio.h>
#include <unistd.h>
#include <dirent.h> 
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

static volatile unsigned short *syscon;
volatile unsigned int *mvgpioregs; // GPIO
volatile unsigned int *mvmfpregs; // Multi Function Pin (mux)

static inline unsigned short PEEK16(unsigned long addr) {
	unsigned short ret;

	asm volatile (
		"ldrh %0, [ %1 ]\n"
		: "=r" (ret)
		: "r" (addr)
		: "memory"
	);
	return ret;
}

static inline void POKE16(unsigned long addr, unsigned short dat) {
	asm volatile (
		"strh %1, [ %0 ]\n"
		:
		: "r" (addr), "r" (dat)
		: "memory"
	);
}

#define peek16(adr) PEEK16((unsigned long)&syscon[(adr)/2])
#define poke16(adr, val) POKE16((unsigned long)&syscon[(adr)/2],(val))
static void i2c_pause(void) { int k; for (k=0;k<100;k++) peek16(0); }
#define scl_z do { i2c_pause(); *z = (1 << 24); } while(0)
#define scl_0 do { i2c_pause(); *y = (1 << 24); } while(0)
#define sda_z do { i2c_pause(); *z = (1 << 23); } while(0)
#define sda_0 do { i2c_pause(); *y = (1 << 23); } while(0)
#define sda_in (x[0x008/4] & (1 << 23))

static unsigned char uc_i2c_7bit_adr = 0x2a;
#define uc_read(nbytes, dat) uc_op((nbytes)|0x100, (dat))
#define uc_write(nbytes, dat) uc_op((nbytes), (dat))
int uc_op(int op, unsigned char *dat) {
       unsigned int d, i, ack;
       unsigned int nbytes = op & 0xff;
       volatile unsigned int *x = mvgpioregs;
       volatile unsigned int *y = &x[0x5c/4];
       volatile unsigned int *z = &x[0x68/4];

       scl_z; /* tristate, scl/sda pulled up */
       sda_z;

       sda_0; /* i2c, start (sda low) */
       for (d = (uc_i2c_7bit_adr<<1)|((op&0x100)?1:0), i = 0;
          i < 8; i++, d <<= 1) {
               scl_0; /* scl low */
               if (d & 0x80) sda_z; else sda_0;
               scl_z; /* scl high */
       }
       scl_0;
       sda_z; /* scl low, tristate sda */
       scl_z; /* scl high, tristate sda */
       ack = sda_in; /* sample ack */
       if (ack) return -1;

       if (op>>8) while (nbytes) { /* i2c read */
               for (d = i = 0; i < 8; i++) {
                       scl_0; /* scl low, tri sda */
                       sda_z;
                       scl_z; /* scl high, tri sda */
                       scl_z; /* scl high, tri sda (for timing) */
                       d <<= 1;
                       d |= sda_in ? 1 : 0;
               }
               scl_0;
               nbytes--;
               if (nbytes == 0) sda_z; else sda_0; /* ack if more */
               scl_z;
               *dat++ = d;
       } else while (nbytes) { /* i2c write */
               for (d = *dat++, i = 0; i < 8; i++, d <<= 1) {
                       scl_0; /* scl low, sda low */
                       if (d & 0x80) sda_z; else sda_0;
                       scl_z; /* scl high */
               }
               scl_0; /* scl low, tristate sda */
               sda_z;
               scl_z; /* scl high, tristate sda */
               ack = sda_in; /* sample ack */
               assert(ack == 0);
               nbytes--;
       }

       scl_0;
       sda_0;
       scl_z;
       sda_z; /* i2c stop */

       return 0;
}

// Scale voltage to silabs 0-2.5V
uint16_t inline sscale(uint16_t data){
	return data * (2.5/1023) * 1000;
}

// Scale voltage for resistor dividers
uint16_t inline rscale(uint16_t data, uint16_t r1, uint16_t r2)
{
	uint16_t ret = (data * (r1 + r2)/r2);
	return sscale(ret);
}

void do_sleep(int deciseconds)
{
	unsigned char dat[4];

	dat[0] = (deciseconds >> 24) & 0xff;
	dat[1] = (deciseconds >> 16) & 0xff;
	dat[2] = (deciseconds >> 8) & 0xff;
	dat[3] = (deciseconds & 0xff);
	uc_write(4, dat);
}

void do_info()
{
	uint16_t data[10];
	uint8_t tmp[20];
	bzero(tmp, 20);
	int i, ret;

	ret = uc_read(20, tmp);
	assert(ret != -1);

	for (i = 0; i < 10; i++)
		data[i] = (tmp[i*2] << 8) | tmp[(i*2)+1];

	/* P2.0-P2.1, P2.3-P2.6 */
	/* 0x8-0x9, 0xb-0xe */
	printf("VIN=%d\n", rscale(data[0], 4750, 205));
	printf("V5_A=%d\n", rscale(data[1], 205, 154));
	printf("V1P2=%d\n", sscale(data[2]));
	printf("DDR_1P5V=%d\n", sscale(data[3]));
	printf("V1P8=%d\n", sscale(data[4]));
	printf("CPU_CORE=%d\n", sscale(data[5]));
}

static void usage(char **argv) {
	fprintf(stderr, "Usage: %s [OPTION] ...\n"
	  "Technologic Systems Microcontroller Access\n"
	  "\n"
	  "  -h, --help            This message\n"
	  "  -i, --info            Read all Silabs ADC values and rev\n"
	  "  -s, --sleep <deciseconds> Put the board in a sleep mode for n deciseconds\n"
	  "    All values are returned in mV unless otherwise labelled\n\n",
	  argv[0]
	);
}

int main(int argc, char **argv)
{
	int c;
	int devmem;

	static struct option long_options[] = {
	  { "info", 0, 0, 'i' },
	  { "sleep", 1, 0, 's' },
	  { "help", 0, 0, 'h' },
	  { 0, 0, 0, 0 }
	};

	if(argc == 1) {
		usage(argv);
		return(1);
	}

	devmem = open("/dev/mem", O_RDWR|O_SYNC);
	assert(devmem != -1);

	mvgpioregs = (unsigned int *) mmap(0, 4096,
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0xd4019000);
	syscon = (unsigned short *) mmap(0, 4096,
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80004000);
	mvmfpregs = (unsigned int *) mmap(0, 4096,
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0xd401e000);

	mvmfpregs[0x1ec/4] = 0x4c06; /* SCL to GPIO */
	mvmfpregs[0x1f0/4] = 0x4c06; /* SDA to GPIO */
	mvgpioregs[0x02c/4] = (3 << 23); /* 0 for output register */

	while((c = getopt_long(argc, argv, "is:h", long_options, NULL)) != -1) {
		switch (c) {
		case 'i':
			do_info();
			break;
		case 's':
			do_sleep(atoi(optarg));
			break;
		case 'h':
		default:
			usage(argv);
		}
	}

	mvmfpregs[0x1ec/4] = 0x4c00; /* SCL back to I2C */
	mvmfpregs[0x1f0/4] = 0x4c00; /* SDA back to I2C */

	return 0;
}

