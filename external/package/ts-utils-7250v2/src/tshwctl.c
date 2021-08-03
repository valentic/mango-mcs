/*  Copyright 2004-2009, Unpublished Work of Technologic Systems
 *  All Rights Reserved.
 *
 *  THIS WORK IS AN UNPUBLISHED WORK AND CONTAINS CONFIDENTIAL,
 *  PROPRIETARY AND TRADE SECRET INFORMATION OF TECHNOLOGIC SYSTEMS.
 *  ACCESS TO THIS WORK IS RESTRICTED TO (I) TECHNOLOGIC SYSTEMS EMPLOYEES
 *  WHO HAVE A NEED TO KNOW TO PERFORM TASKS WITHIN THE SCOPE OF THEIR
 *  ASSIGNMENTS  AND (II) ENTITIES OTHER THAN TECHNOLOGIC SYSTEMS WHO
 *  HAVE ENTERED INTO  APPROPRIATE LICENSE AGREEMENTS.  NO PART OF THIS
 *  WORK MAY BE USED, PRACTICED, PERFORMED, COPIED, DISTRIBUTED, REVISED,
 *  MODIFIED, TRANSLATED, ABRIDGED, CONDENSED, EXPANDED, COLLECTED,
 *  COMPILED,LINKED,RECAST, TRANSFORMED, ADAPTED IN ANY FORM OR BY ANY
 *  MEANS,MANUAL, MECHANICAL, CHEMICAL, ELECTRICAL, ELECTRONIC, OPTICAL,
 *  BIOLOGICAL, OR OTHERWISE WITHOUT THE PRIOR WRITTEN PERMISSION AND
 *  CONSENT OF TECHNOLOGIC SYSTEMS . ANY USE OR EXPLOITATION OF THIS WORK
 *  WITHOUT THE PRIOR WRITTEN CONSENT OF TECHNOLOGIC SYSTEMS  COULD
 *  SUBJECT THE PERPETRATOR TO CRIMINAL AND CIVIL LIABILITY.
 */
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>
#include <limits.h>
#include <sched.h>
#include <libgen.h>
#include <time.h>
#include <sys/timex.h>
#include <signal.h>

#include "mvdmastream.h"
#include "evgpio.h"

#define DIOMASK 0x0fffffff07ff7fffULL

extern signed char ispVM( const char * a_pszFilename );
static volatile unsigned short *syscon;
static void *mpmu_base;
static void *pmua_base;
volatile unsigned int *mvgpioregs; // GPIO
volatile unsigned int *mvmfpregs; // Multi Function Pin (mux)
static volatile unsigned long *fe_base;
static volatile int timed_out;
static void calculateClocks(unsigned int *pclk, unsigned int *dclk);
static int onboardsw = 1;

#ifndef BIT
#define BIT(x) (1<<(x))
#endif

/* a.k.a. APMU, Application Subsystem Power Management (-0x2800!!!) */
#define PMUA_BASE_PHYSICAL 0xD4280000    
/* Main Power Management Unit, a.k.a. PMUM */
#define MPMU_BASE_PHYSICAL 0xD4050000     

#define FE_BASE_PHYSICAL 0xC0800000
#define SMI_RDVALID     (1 << 27)
#define SMI_BUSY        (1 << 28)

#define GLOBAL_REGS_1   0x1f
#define GLOBAL_REGS_2   0x17

#define VTU_OPS_REGISTER  0x05  /* offset in GLOBAL_REGS_1 */
#define VTU_BUSY     BIT(15)
#define VTU_OP_FLUSHALL          (1 << 12)
#define VTU_OP_LOAD              (3 << 12)
#define VTU_OP_GET_NEXT          (4 << 12)
#define VTU_OP_GET_CLR_VIOLATION (7 << 12)

#define VTU_VID_REG     0x06  /* offset in GLOBAL_REGS_1 */
#define VTU_VID_VALID            BIT(12)

struct vtu {
	int v, vid;
	unsigned char tags[7];
	unsigned char forceVID[7];
};

static unsigned short model;
static int cpu_port;

static volatile unsigned short *syscon;

#define FE_BASE_PHYSICAL 0xC0800000
#define SMI_RDVALID     (1 << 27)
#define SMI_BUSY        (1 << 28)

 
#define FPGA_SYSCON_PHYSICAL    0x80004000
#define DIO27  1
#define DIO42  (1<<15)
#define MDIO   DIO27
#define MDC    DIO42

// See Xilinx UG380 v2.3 page 126
// This bitstream is just a command to load the other bitstream from address
// 0x700000 of the flash.
unsigned short bitstream[] = {
   
  0xffff, 0xffff,
  0xaa99, 0x5566,
  0x3261, 0x0000,
  0x3281, 0x6b70,
  0x32a1, 0x0000,
  0x32c1, 0x6b70,
  //0x30a1, 0x0000,
  0x30a1, 0x000e,
  0x2000, 0x2000,
  0x2000, 0x2000,
  0x2000, 0x2000
};

#define NWORDS  (sizeof(bitstream) / sizeof(unsigned short))

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

static int semid = -1;
static int slocked = 0;
void sunlock(void) {
	struct sembuf sop = { 0, 1, SEM_UNDO};
	int r;
	if (!slocked) return;
	r = semop(semid, &sop, 1);
	assert (r == 0);
	slocked = 0;
}


void slock(void) {
	int r;
	struct sembuf sop;
	if (slocked) return;
	if (semid == -1) {
		key_t semkey;
		semkey = 0x75000000;
		semid = semget(semkey, 1, IPC_CREAT|IPC_EXCL|0777);
		if (semid != -1) {
			sop.sem_num = 0;
			sop.sem_op = 1;
			sop.sem_flg = 0;
			r = semop(semid, &sop, 1);
			assert (r != -1);
		} else semid = semget(semkey, 1, 0777);
		assert (semid != -1);
	}
	sop.sem_num = 0;
	sop.sem_op = -1;
	sop.sem_flg = SEM_UNDO;
	r = semop(semid, &sop, 1);
	assert (r == 0);
	slocked = 1;
	atexit(sunlock);
}


static void i2c_pause(void) { int k; for (k=0;k<3;k++) peek16(0); }
// The following functions are for the Microchip 24LC08B EEPROM.
#define EE_SCL ( 1 << 17 )
#define EE_SDA ( 1 << 24 )
#define ee_scl_z do { i2c_pause(); mvgpioregs[0x60/4] = EE_SCL; } while(0)
#define ee_scl_0 do { i2c_pause(); mvgpioregs[0x54/4] = EE_SCL; } while(0)
#define ee_sda_z do { i2c_pause(); mvgpioregs[0x60/4] = EE_SDA; } while(0)
#define ee_sda_0 do { i2c_pause(); mvgpioregs[0x54/4] = EE_SDA; } while(0)
#define ee_sda_in (mvgpioregs[0x00/4] & EE_SDA)
#define EE_ADR 0xa0
#define EE_READ 0x01

// ee_byte(x) clocks out a byte on the I2C bus.  It has no knowledge of stop,
// start, or ack conditions
unsigned char ee_byte(unsigned char x) {
	int i;
	//unsigned char x = x;
	for (i = 0; i != 8; i++) {
		ee_scl_0;
		if (x & 0x80) ee_sda_z;
		else ee_sda_0;
		x = x << 1;
		ee_scl_z;
		ee_scl_z;
		if (ee_sda_in) x |= 1;
	}
	return x;
}

int ee_rx_ack() {
	ee_scl_0;
	ee_sda_z;
	ee_scl_z;
	ee_scl_z;
	return ee_sda_in ? 0 : 1;
}

void ee_start(){ ee_sda_0; }
void ee_stop(){
	ee_scl_0;
	ee_sda_0;
	ee_scl_z;
	ee_sda_z;
}

// ee_adr(adr) is used to begin any transaction.  Bus must be in an idle state
// before calling.  After this function, bus is in an active state and the 
// EEPROM is ready for a write data byte.
void ee_adr(unsigned int adr) {

	ee_start();
	ee_byte(EE_ADR | (adr >> 8));
	if (!ee_rx_ack()) { // if EEPROM doesn't ack, it is busy, so start over
		ee_stop();
		ee_adr(adr);
	} else {
		ee_byte(adr & 0xff);
		assert(ee_rx_ack());
	}
}

void ee_page_write(unsigned int adr, char *buf, int len) {
	int i;

	ee_adr(adr);
	for (i = 0; i < len; i++) {
		ee_byte(buf[i]);
		assert(ee_rx_ack());
	}
	ee_stop();
}

void ee_write(unsigned int adr, char *buf, int len) {
	// write transactions cannot cross 16 byte page boundaries

	int l;

	l = 16 - (adr & 0xf);
	if (len > l) {
		ee_page_write(adr, buf, l);
		ee_write(adr + l, buf + l, len - l);
	} else {
		ee_page_write(adr, buf, len);
	}
}

void ee_read(unsigned int adr, char *buf, int len) {
	int i;
	// to do a read, you send the address as if doing a write, the a new
	// start condition, then a read command
	ee_adr(adr);
	ee_scl_0;
	ee_sda_z;
	ee_scl_z;
	ee_sda_0;
	ee_byte(EE_ADR | EE_READ | (adr >> 8));
	assert(ee_rx_ack());
	for (i = 0; i < len; i++) {
		buf[i] = ee_byte(0xff);
		ee_scl_0;
		ee_sda_0; // send ACK
		ee_scl_z;
	}
	ee_stop();
}

void sspi_cmd(unsigned int cmd) {
	unsigned int i;
	unsigned int s = peek16(0x28) & 0xfff0;

	// pulse CS#
	poke16(0x28, s | 0x2);
	poke16(0x28, s | 0x0);

	for (i = 0; i < 32; i++, cmd <<= 1) {
		if (cmd & 0x80) {
			poke16(0x28, s | 0x4);
			poke16(0x28, s | 0xc);
		} else {
			poke16(0x28, s | 0x0);
			poke16(0x28, s | 0x8);
		}
	}
}

int read_tagmem_id() {
	int i;
	unsigned int ret;
	unsigned int s = peek16(0x28) & 0xfff0;

	sspi_cmd(0xac); // X_PROGRAM_EN
	sspi_cmd(0x98); // READ_ID
	for (ret = 0x0, i = 0; i < 32; i++) {
		poke16(0x28, s | 0x0);
		poke16(0x28, s | 0x8);
		ret = ret << 1 | (peek16(0x28) & 0x1);
	}
	sspi_cmd(0x78); // PROGRAM_DIS
	poke16(0x28, s | 0x2);
	return ret;

}

/* Note: On the TS-4740, there is no tagmem, but the EEPROM serves to replace
 * it.  The first 96 bytes of EEPROM have the exact tagmem data layout.
 */

static int read_tagmem(unsigned int *tagmem) {
	int i, j;
	unsigned int ret;
	unsigned int s = peek16(0x28) & 0xfff0;

	if (model == 0x4740) {
		ee_read(0, (unsigned char *)tagmem, 96);
		return 1;
	}

	sspi_cmd(0xac); // X_PROGRAM_EN
	sspi_cmd(0x4e); // READ_TAG

	for (j = 0; j < 24; j++) {
		for (ret = 0x0, i = 0; i < 32; i++) {
			poke16(0x28, s | 0x0);
			poke16(0x28, s | 0x8);
			ret = ret << 1 | (peek16(0x28) & 0x1);
		}
		tagmem[j] = ret;
	}

	sspi_cmd(0x78); // PROGRAM_DIS

	poke16(0x28, s | 0x2);
	return 1;
}

static int write_tagmem(unsigned int *tagmem) {
	int i, j;
	unsigned int s = peek16(0x28) & 0xfff0;
	unsigned int id;

	if (model == 0x4740) {
		for (i=8; i<24; i++) tagmem[i] = i; // XXX
		ee_write(0, (unsigned char *)tagmem, 96);
		return 1;
	}

	id = read_tagmem_id();
	assert(id == 0xc2099480 || id == 0xc2059480);
	// 0xc209 = LFXP2-5E, 79 bytes tagmem
	// 0xc205 = LFXP2-8E, 96 bytes tagmem

	sspi_cmd(0xac); // X_PROGRAM_EN
	sspi_cmd(0x8e); // WRITE_TAG

	for (j = 0; j < 24; j++) {
		unsigned int x = tagmem[j];
		for (i = 0; i < 32; i++, x <<= 1) {
			if (x & 0x80000000UL) {
				poke16(0x28, s | 0x4);
				poke16(0x28, s | 0xc);
			} else {
				poke16(0x28, s | 0x0);
				poke16(0x28, s | 0x8);
			}
			if (i == 23 && j == 19 && id == 0xc2099480) break;
		}
		if (i == 23 && j == 19 && id == 0xc2099480) break;
	}

	for (i = 0; i < 8; i++) {
		poke16(0x28, s | 0x2);
		poke16(0x28, s | 0xa);
	}
	poke16(0x28, s | 0x2);
	usleep(25000);
	return 1;
}

int erase_tagmem() {
	int i;
	unsigned int s = peek16(0x28) & 0xfff0;

	if (model == 0x4740) return 1; // no need to erase TS-4740 EEPROM

	sspi_cmd(0xac); // X_PROGRAM_EN
	sspi_cmd(0xe); // ERASE_TAG

	for (i = 0; i < 8; i++) {
		poke16(0x28, s | 0x2);
		poke16(0x28, s | 0xa);
	}
	poke16(0x28, s | 0x2);
	usleep(1000000);
	return 1;
}


static unsigned char rtc_i2c_7bit_adr = 0x6f;
#define scl_z do { i2c_pause(); *z = (1 << 24); } while(0)
#define scl_0 do { i2c_pause(); *y = (1 << 24); } while(0)
#define sda_z do { i2c_pause(); *z = (1 << 23); } while(0)
#define sda_0 do { i2c_pause(); *y = (1 << 23); } while(0)
#define sda_in (x[0x008/4] & (1 << 23))

#define rtc_read(reg) rtc_op((reg))
#define rtc_write(reg, val) rtc_op((reg)|((val&0xff)<<8)|(1<<16))
#define rtc_done() rtc_op(-1)
int rtc_op(int op) {
	unsigned int d, i, ack, ret;
	unsigned int reg = op & 0xff;
	static int rtc_state, printed; 
	volatile unsigned int *x = mvgpioregs;
	volatile unsigned int *y = &x[0x5c/4];
	volatile unsigned int *z = &x[0x68/4];
	if (rtc_state == -1) return -1;
	else if (rtc_state > 0 && rtc_state == reg) { /* parked read hit */
		scl_0; /* scl low, tri sda */
		sda_0; /* scl low, sda low */
		scl_z; /* scl high, sda low */
		goto rxbyte;
	} else if (rtc_state > 0 && (rtc_state == (0x100|reg))) { 
	/* parked write hit */
		goto txbyte;
	} else if (rtc_state & 0x100) { /* parked write miss */
		sda_0;
		scl_0;
	} else if (rtc_state) { /* parked read miss */
		scl_0; /* scl low, tri sda */
		sda_z; /* scl low, sda high */
		scl_z; /* scl high, sda hi */
		scl_0; /* scl low, tristate sda */
		sda_0; /* scl low, sda low */
	}

	scl_z; /* tristate, scl/sda pulled up */
	sda_z;

	if (op == -1) {
		rtc_state = 0;
		return 0;
	}

	sda_0; /* i2c, start (sda low) */
	for (d = rtc_i2c_7bit_adr<<1, i = 0; i < 8; i++, d <<= 1) {
		scl_0; /* scl low */
		if (d & 0x80) sda_z; else sda_0;
		scl_z; /* scl high */
	}
	scl_0;
	sda_z; /* scl low, tristate sda */
	scl_z; /* scl high, tristate sda */
	ack = sda_in; /* sample ack */
	if (rtc_state == 0) {
		if (!printed) printf("rtc_present=%d\n", ack ? 0 : 1);
		printed=1;
		if (ack) {
			rtc_state = -1;
			return -1;
		}
	}
	assert(ack == 0);
	sda_0;
	for (d = reg, i = 0; i < 8; i++, d <<= 1) {
		scl_0; /* scl low, sda low */
		if (d & 0x80) sda_z; else sda_0;
		scl_z; /* scl high */
	}
	scl_0; /* scl low, tristate sda */
	sda_z;
	scl_z; /* scl high, tristate sda */
	ack = sda_in; /* sample ack */
	assert(ack == 0);

	if (op>>16) goto txbyte;

	scl_0; /* scl low, tristate sda */
	scl_0; /* scl low, sda high */
	scl_z; /* scl high, sda high */
	sda_0; /* scl high, sda lo (repeated start) */
	for (d = rtc_i2c_7bit_adr<<1|1, i = 0; i < 8; i++, d <<= 1) {
		scl_0; /* scl low */
		if (d & 0x80) sda_z; else sda_0;
		scl_z; /* scl high */
	}
	scl_0; /* scl low, tristate sda */
	sda_z;
	scl_z; /* scl high, tristate sda */
	ack = sda_in; /* sample ack */
	assert(ack == 0);

	rxbyte:
	for (ret = i = 0; i < 8; i++) {
		scl_0; /* scl low, tri sda */
		sda_z;
		scl_z; /* scl high, tri sda */
		scl_z; /* scl high, tri sda (for timing) */
		ret <<= 1;
		ret |= sda_in ? 1 : 0;
	}
	rtc_state = (reg + 1) & 0xff;
	return ret;

	txbyte:
	for (d = op>>8, i = 0; i < 8; i++, d <<= 1) {
		scl_0; /* scl low, sda low */
		if (d & 0x80) sda_z; else sda_0;
		scl_z; /* scl high */
	}
	scl_0; /* scl low, tristate sda */
	sda_z;
	scl_z; /* scl high, tristate sda */
	ack = sda_in; /* sample ack */
	assert(ack == 0);
	rtc_state = ((reg + 1) & 0xff) | 0x100;
	return 0;
}

static int smi_read(unsigned long phy, unsigned long reg, unsigned short *data) {
	unsigned long x;
	int i;

	for (i = 0; (i < 10) && (fe_base[0x10 / 4] & SMI_BUSY); i++)
	{
		usleep(10000);
		if (i == 9) return -1;
	}

	fe_base[0x10 / 4] = (phy << 16) | (reg << 21) | (1 << 26); 

	for (i = 0; (i < 10) && (fe_base[0x10 / 4] & SMI_BUSY); i++)
	{
		usleep(10000);
		if (i == 9) return -1;
	}

	for (i = 0; (i < 10) && (!((x = fe_base[0x10 / 4]) & SMI_RDVALID)); i++)
	{
		usleep(10000);
		if (i == 9) return -1;
	}

	*data = x & 0xFFFF;

	return 0;
}

static int smi_write(unsigned long phy, unsigned long reg, unsigned short data) {
	int i;
	
	for (i = 0; (i < 10) && (fe_base[0x10 / 4] & SMI_BUSY); i++)
	{
		usleep(10000);
		if (i == 9) return -1;
	}

	fe_base[0x10 / 4] = (phy << 16) | (reg << 21) | data;

	for (i = 0; (i < 10) && (fe_base[0x10 / 4] & SMI_BUSY); i++)
	{
		usleep(10000);
		if (i == 9) return -1;
	}

	return 0;
}

#define MDIO_HI  (((a) |= MDIO), poke16(0x14, a)) 
#define MDIO_LO   (((a) &= ~MDIO), poke16(0x14, a))
#define MDC_HI  (((a) |= MDC), poke16(0x14, a)) 
#define MDC_LO   (((a) &= ~MDC), poke16(0x14, a))

static int bb_phy_write(unsigned long phy, unsigned long reg, unsigned short data) {
	int x;
	unsigned int a,b;

	a = peek16(0x1C);
	a |= 0x8001;   /* Set both as outputs */
	poke16(0x1C, a);

	a = peek16(0x14); /* read current pin values */

	MDC_LO;
	MDIO_HI;

	/* preamble, toggle clock high then low */
	for(x=0; x < 32; x++) {
		MDC_HI;   
		MDC_LO;            
	}
	/* preamble ends with MDIO high and MDC low */

	/* start bit followed by write bit */
	for(x=0; x < 2; x++) {      
		MDIO_LO;      
		MDC_HI;   
		MDC_LO;      
		MDIO_HI;
		MDC_HI;   
		MDC_LO;      
	}
	/* ends with MDIO high and MDC low */

	/* send the PHY address, starting with MSB */
	b = 0x10;
	for(x=0; x < 5; x++) {
		if (phy & b)
			MDIO_HI;
		else
			MDIO_LO;

		MDC_HI;   
		MDC_LO;      

		b >>= 1;
	}
	/* ends with MDC low, MDIO indeterminate */

	/* send the register address, starting with MSB */
	b = 0x10;
	for(x=0; x < 5; x++) {
		if (reg & b)
			MDIO_HI;
		else
			MDIO_LO;

		MDC_HI;   
		MDC_LO;      

		b >>= 1;
	}

	/* ends with MDC low, MDIO indeterminate */

	/* clock a one, then clock a zero */
	MDIO_HI;
	MDC_HI;   
	MDC_LO;      

	MDIO_LO;
	MDC_HI;   
	MDC_LO;

	/* send the data, starting with MSB */
	b = 0x8000;
	for(x=0; x < 16; x++) {
		if (data & b)
			MDIO_HI;
		else
			MDIO_LO;

		MDC_HI;   
		MDC_LO;      

		b >>= 1;
	}

	a = peek16(0x1C);
	a &= ~0x8001;   /* Set both as inputs */
	poke16(0x1C, a);

	return 0;
}

static int bb_phy_read(unsigned long phy, unsigned long reg, unsigned short *data) {
	int x, d;
	unsigned int a,b;

	d = 0;

	a = peek16(0x1C);
	a |= 0x8001;   /* Set both as outputs */
	poke16(0x1C, a);

	a = peek16(0x14); /* read current pin values */

	MDC_LO;
	MDIO_HI;

	/* preamble, toggle clock high then low */
	for(x=0; x < 32; x++) {
		MDC_HI;   
		MDC_LO;            
	}
	/* preamble ends with MDIO high and MDC low */

	/* start bit */         
	MDIO_LO; MDC_HI; MDC_LO;      
	MDIO_HI; MDC_HI; MDC_LO;      
	MDC_HI;  MDC_LO;      
	MDIO_LO; MDC_HI; MDC_LO;

	/* send the PHY address, starting with MSB */
	b = 0x10;
	for(x=0; x < 5; x++) {
		if (phy & b)
			MDIO_HI;
		else
			MDIO_LO;

		MDC_HI;   
		MDC_LO;      

		b >>= 1;
	}
	/* ends with MDC low, MDIO indeterminate */

	/* send the register address, starting with MSB */
	b = 0x10;
	for(x=0; x < 5; x++) {
		if (reg & b)
			MDIO_HI;
		else
			MDIO_LO;

		MDC_HI;   
		MDC_LO;      

		b >>= 1;
	}

	/* ends with MDC low, MDIO indeterminate */
	a = peek16(0x1C);
	a &= ~0x0001;   /* Set MDIO as input */
	poke16(0x1C, a);

	/* another clock */
	MDC_HI;   
	MDC_LO;      

	/* read the data, starting with MSB */
	b = 0x8000;
	for(x=0; x < 16; x++) {
		MDC_HI;
		a  = peek16(0x24);

		if (a & 1)
			d |= b;

		MDC_LO;      

		b >>= 1;
	}

	a = peek16(0x1C);
	a &= ~0x8001;   /* Set both as inputs */
	poke16(0x1C, a);

	*data = d;
	return 0;
}

static int phy_read(unsigned long phy, unsigned long reg, unsigned short *data) {
 	int ret;
	if (onboardsw)
		ret = smi_read(phy, reg, data);
	else 
		ret = bb_phy_read(phy, reg, data);

	return ret;
}

static int phy_write(unsigned long phy, unsigned long reg, unsigned short data) {
	int ret;

	if (onboardsw)
		ret = smi_write(phy, reg, data);
	else
		ret = bb_phy_write(phy, reg, data);

	return ret;
}

static uint16_t vtu_readwait(unsigned long reg) {
	uint16_t x;
	do {
		if(phy_read(GLOBAL_REGS_1, reg, &x) < 0) {
			fprintf(stderr, "VTU_Read Timeout to 0x%llX\n", reg);
		}
	} while (x & VTU_BUSY);

	return x;
}

static const char *MemberTagString(int tag) {
	switch(tag) {
		case 0: return "unmodified";
		case 1: return "untagged";
		case 2: return "tagged";
		default: return "unknown";   
	}   
}

inline void switch_errata_3_1(unsigned int n) {  
   volatile unsigned short smicmd, x;
   
   /* Write 0x0003 to PHY register 13 */
   do {
      phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
   smicmd = 0x960D | n;
	phy_write(0x17, 0x19, 0x0003);   /* smi data */		
	phy_write(0x17, 0x18, smicmd);
	
	/* Write 0x0000 to PHY register 14 */
	do {
	   phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
	smicmd = 0x960E | n;
	phy_write(0x17, 0x19, 0);   /* smi data */		
	phy_write(0x17, 0x18, smicmd);
	
	/* Write 0x4003 to PHY register 13 */
	do {
	   phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
   smicmd = 0x960D | n;
	phy_write(0x17, 0x19, 0x4003);   /* smi data */		
	phy_write(0x17, 0x18, smicmd);
	
	/* Write 0x0000 to PHY register 14 */
	do {
	   phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
	smicmd = 0x960E | n;
	phy_write(0x17, 0x19, 0);   /* smi data */		
	phy_write(0x17, 0x18, smicmd);
}

inline void switch_enphy(unsigned long phy) {
   volatile unsigned short x;
   do {
	   phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
	phy_write(0x17, 0x19, 0xb300);
	phy_write(0x17, 0x18, phy);
}

inline void switch_enflood(unsigned long port) {
	uint16_t data;
	phy_read(port, 0x04, &data);
	phy_write(port, 0x04, data | 0xf);
}

inline void switch_enbcastflood(unsigned long port) {
	phy_write(port, 0x04, 0x7f);
}

inline void switch_forcelink(unsigned long port) {
	uint16_t data;
	phy_read(port, 0x01, &data);
	phy_write(port, 0x01, data | 0x30);
}

// Set destination port for STP/IGMP/MLD
inline void switch_cpudest(unsigned long port) {
	uint16_t data;
	phy_read(port, 0x1a, &data);
	data |= ((port << 4) & 0xf0);
	phy_write(port, 0x1a, data);
}

static int vtu_add_entry(struct vtu *entry) {
	int i, j, p;   
	unsigned short r7, r8, x;
	unsigned short r6[7];

	while(1) {
		if (phy_read(GLOBAL_REGS_1, VTU_OPS_REGISTER, &x) < 0) {
		fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
		return -1;
	} 

	if (! (x & VTU_BUSY))  /* wait for VB=0 in Global Reg 0x05 */
		break;
	}

	phy_write(GLOBAL_REGS_1, VTU_VID_REG, VTU_VID_VALID | entry->vid);

	r7 = entry->tags[0] | (entry->tags[1] << 4) | (entry->tags[2] << 8) | 
	  (entry->tags[3] << 12);
	r8 = entry->tags[4] | (entry->tags[5] << 4) | (entry->tags[6] << 8);

	phy_write(GLOBAL_REGS_1, 0x07, r7);   
	phy_write(GLOBAL_REGS_1, 0x08, r8);   
	phy_write(GLOBAL_REGS_1, VTU_OPS_REGISTER, 0xb000);   /* start the load */                                    

	/* Next, we take care of the VLAN map, which is a per-port 
	bitfield at offset 6 */

	while(1) {
		if (phy_read(GLOBAL_REGS_1, VTU_OPS_REGISTER, &x) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		} 

		if (! (x & VTU_BUSY))  /* wait for VB=0 in Global Reg 0x05 */
			break;
	}

	phy_write(GLOBAL_REGS_1, VTU_VID_REG, 0xFFF);

	i=j=0;
	memset(&r6, 0, sizeof(r6));

	while(1) {
		x = 0;
		phy_write(GLOBAL_REGS_1, VTU_OPS_REGISTER, VTU_BUSY | VTU_OP_GET_NEXT);  

		while(1) {
			if (phy_read(GLOBAL_REGS_1, VTU_OPS_REGISTER, &x) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
			} 

			if (! (x & VTU_BUSY))  /* wait for VB=0 in Global Reg 0x05 */
			break;
		}

		if (phy_read(GLOBAL_REGS_1, VTU_VID_REG, &x) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		}

		if ((x & 0xFFF) == 0xFFF)
		break;

		j++;

		if (phy_read(GLOBAL_REGS_1, 0x07, &r7) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		}

		if (phy_read(GLOBAL_REGS_1, 0x08, &r8) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		}

		for(p=0; p < 7; p++) {
			switch(p) {
				case 0: if ((r7 & 0x3) != 0x3)  goto L1; break;
				case 1: if ((r7 & 0x30) != 0x30)  goto L1; break;
				case 2: if ((r7 & 0x300) != 0x300)  goto L1; break;
				case 3: if ((r7 & 0x3000) != 0x3000)  goto L1; break;

				case 4: if ((r8 & 0x3) != 0x3)  goto L1; break;
				case 5: if ((r8 & 0x30) != 0x30)  goto L1; break;
				case 6: if ((r8 & 0x300) != 0x300)  goto L1; break;
			}

			continue;

			L1:;   
			for(i=0; i < 7; i++) {
				if (i != p) {  /* don't set "our" bit in "our" mask register */            
					switch(i) {
						case 0: if ((r7 & 0x3) != 0x3) { r6[p] |= BIT(i);  } break; 
						case 1: if ((r7 & 0x30) != 0x30) { r6[p] |= BIT(i); } break;                                 
						case 2: if ((r7 & 0x300) != 0x300) { r6[p] |= BIT(i); } break; 
						case 3: if ((r7 & 0x3000) != 0x3000) { r6[p] |= BIT(i);  }  break;
						case 4: if ((r8 & 0x3) != 0x3) { r6[p] |= BIT(i);  } break; 
						case 5: if ((r8 & 0x30) != 0x30) { r6[p] |= BIT(i); } break;                                 
						case 6: if ((r8 & 0x300) != 0x300) { r6[p] |= BIT(i); } break;   
					}
				}
				else if (p != cpu_port) {
					if (entry->tags[p] != 3) {
						if (entry->forceVID[p])
							phy_write(0x18 + p, 0x7, 0x1000 | entry->vid);
						else
							phy_write(0x18 + p, 0x7, entry->vid);
					}
				}                        
			}  
		}      
	}   

	for(p=0; p < 7; p++) {
		phy_write(0x18 + p, 0x6, r6[p]);

		if (entry->tags[p] != 3)
			phy_write(0x18 + p, 0x8, 0x480);         
	}

	return 0;
}


/*static int vtu_del_entry(unsigned int vid) {
	unsigned short x;
	     
	while(1) {
		if (phy_read(GLOBAL_REGS_1, VTU_OPS_REGISTER, &x) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		} 

		if (! (x & VTU_BUSY))  // wait for VB=0 in Global Reg 0x05 
			break;
	}

	phy_write(GLOBAL_REGS_1, VTU_VID_REG, vid);   
	// Note that the V (valid) bit is not set,
	// making this a purge operation 
	phy_write(GLOBAL_REGS_1, VTU_OPS_REGISTER, 0xb000);  // tart the load/purge

	while(1) {
		if (phy_read(GLOBAL_REGS_1, VTU_OPS_REGISTER, &x) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		} 

		if (! (x & VTU_BUSY))  // wait for VB=0 in Global Reg 0x05 
			break;
	}

	return 0;
}*/

static void usage(char **argv) {
	fprintf(stderr, "Usage: %s [OPTION] ...\n"
	  "Technologic Systems TS-471x / TS-77XX FPGA manipulation.\n"
	  "\n"
	  "General options:\n"
	  "  -g, --getmac            Display ethernet MAC address\n"
	  "  -s, --setmac=MAC        Set ethernet MAC address\n"
	  "  -R, --reboot            Reboot the board\n"
	  "  -t, --getrtc            Get system time from RTC time/date\n"
	  "  -S, --setrtc            Set RTC time/date from system time\n"
	  "  -F, --rtctemp           Print RTC temperature\n"
	  "  -p, --rtcinfo           Print RTC info\n"
	  "  -v, --nvram             Get/Set RTC NVRAM\n"
	  "  -i, --info              Display board FPGA info\n"
	  "  -e, --greenledon        Turn green LED on\n"
	  "  -b, --greenledoff       Turn green LED off\n"
	  "  -c, --redledon          Turn red LED on\n"
	  "  -d, --redledoff         Turn red LED off\n"
	  "  -D, --setdio=<pin>      Sets DDR and asserts a specified pin\n"
	  "  -O, --clrdio=<pin>      Sets DDR and deasserts a specified pin\n"
	  "  -G, --getdio=<pin>      Sets DDR and gets DIO pin input value\n"
	  "  -x, --random            Get 16-bit hardware random number\n"
	  "  -W, --watchdog          Daemonize and set up /dev/watchdog\n"
	  "  -A, --autofeed=SETTING  Daemonize and auto feed watchdog\n"
	  "  -n, --setrng            Seed the kernel random number generator\n"
	  "  -X, --resetswitchon     Enable reset switch\n"
	  "  -Y, --resetswitchoff    Disable reset switch\n"
	  "  -l, --loadfpga=FILE     Load FPGA bitstream from FILE\n"
	  "  -L, --loadsp6           Load Spartan6 FPGA from flash (TS-4740)\n"
	  "  -M, --loadlm32=FILE     Load Mico32 RAM with FILE and execute (TS-4740)\n"
	  "  -q, --cputemp           Display the CPU die temperature\n"
	  "  -U, --removejp=JP       Remove soft jumper numbered JP (1-8)\n"
	  "  -J, --setjp=JP          Set soft jumper numbered JP (1-8)\n"
	  "  -k, --txenon=XUART(s)   Enables the TX Enable for an XUART\n"
	  "  -K, --txenoff=XUART(s)  Disables a specified TX Enable\n"
	  "  -N, --canon=PORT(s)     Enables a CAN port\n"
	  "  -f, --canoff=PORT(s)    Disables a CAN port\n"
	  "  -j, --bbclkon           Enables a 12.5MHz clock on DIO 3\n"
	  "  -E, --bbclk2on          Enables a 25MHz clock on DIO 34\n"
	  "  -E, --bbclk3on          Enables a 14.3MHz clock on DIO 3\n"
	  "  -H, --bbclkoff          Disables baseboard clock\n"
	  "  -h, --help              This help\n",
	  argv[0]);

	if(model != 0x7700) {
		fprintf(stderr,
		  "  -r, --touchon           Turns the touchscreen controller on\n"
		  "  -T, --touchoff          Turns the touchscreen controller off\n"
		  "  -B, --baseboard         Display baseboard ID\n"
		  "  -a, --adc               Display MCP3428 ADC readings in millivolts\n"
		  "  -P, --ethvlan           Configures a network switch to split each "
		  "port individually in a vlan\n"
		  "  -y, --ethswitch         Configures a network switch to switch all of "
		  "the outside ports to one interface\n"
		  "  -o, --ethwlan           Configures the first network port (A) to "
		  "its own VLAN, and all other ports to a shared switch\n"
		  "  -C, --ethinfo           Retrieves info on the onboard switch\n"
		  "  -m, --ethbb             Modifies other eth options to use the\n"
		  "                          baseboard switch\n"
		);
	}
}

static inline
unsigned char tobcd(unsigned int n) {
	unsigned char ret;
	ret = (n / 10) << 4;
	ret |= n % 10;
	return ret;
}

static inline unsigned int frombcd(unsigned char x) {
	unsigned int ret;
	ret = (x >> 4) * 10;
	ret += x & 0xf;
	return ret;
}

int main(int argc, char **argv) {
	int devmem, i, c;
	unsigned int tagmem[24];
	unsigned int baseboard = 0;
	int opt_getmac = 0, opt_setmac = 0, opt_reboot = 0, opt_getrtc = 0;
	int opt_setrtc = 0, opt_info = 0, opt_greenledon = 0, opt_nvram = 0;
	int opt_greenledoff = 0, opt_redledon = 0, opt_redledoff = 0;
	int opt_setdio = 0, opt_clrdio = 0, opt_getdio = 0, opt_random = 0;
	int opt_watchdog = 0, opt_resetswitchon = 0;
	int opt_setrng = 0, opt_cputemp = 0, opt_resetswitchoff = 0;
	int opt_baseboard = 0, opt_adc = 0;
	int opt_removejp = 0, opt_setjp = 0;
	int opt_rtctemp = 0, opt_touchon = 0, opt_touchoff = 0;
	int opt_bbclkon = 0, opt_bbclkoff = 0, opt_bbclk2on = 0, opt_txenon = 0;
	int opt_txenoff = 0, opt_canon = 0, opt_canoff=0;
	int opt_ethvlan = 0, opt_ethswitch = 0, opt_ethinfo = 0, opt_ethwlan = 0;
	int opt_loadsp6 = 0;

	uint16_t swmod;

	char *can_on = 0, *can_off = 0, *txen_on = 0, *txen_off = 0, *setdio = 0;
	char *clrdio = 0, *getdio = 0, *opt_mac = 0, *opt_loadfpga = 0, *opt_lm32 = 0;
	FILE *ifile = NULL;

	devmem = open("/dev/mem", O_RDWR|O_SYNC);
	assert(devmem != -1);
	syscon = (unsigned short *) mmap(0, 4096,
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80004000);
	mpmu_base = mmap(0, 4096,
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, MPMU_BASE_PHYSICAL);
	pmua_base = mmap(0, 4096+0x2800,
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, PMUA_BASE_PHYSICAL);
	mvmfpregs = (unsigned int *) mmap(0, 4096,
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0xd401e000);
	mvgpioregs = (unsigned int *) mmap(0, 4096,
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0xd4019000);
	fe_base = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, 
	  devmem, FE_BASE_PHYSICAL);

	model = peek16(0);
	if (model == 0x4740) {
		// Certain MFP need to be DIO on the TS-4740.
		// There are probably more that need to be here.
		mvmfpregs[0x090/4] = 0x4c05; /* MFP 17, EEPROM SCL */
		mvmfpregs[0x0ac/4] = 0x4c05; /* MFP 24, EEPROM SDA */
		mvmfpregs[0x098/4] = 0x4c05; /* MFP 19, boot flag Din */
	}

	static struct option long_options[] = {
	  { "help", 0, 0, 'h' },
	  { "nvram", 0, 0, 'v' },
	  { "getmac", 0, 0, 'g' },
	  { "setmac", 1, 0, 's' },
	  { "reboot", 0, 0, 'R' },
	  { "getrtc", 0, 0, 't' },
	  { "setrtc", 0, 0, 'S' },
	  { "rtctemp", 0, 0, 'F' },
	  { "rtcinfo", 0, 0, 'p' },
	  { "greenledon", 0, 0, 'e'},
	  { "greenledoff", 0, 0, 'b'},
	  { "redledon", 0, 0, 'c'},
	  { "redledoff", 0, 0, 'd'},
	  { "setdio", 1, 0, 'D'},
	  { "clrdio", 1, 0, 'O'},
	  { "getdio", 1, 0, 'G'},
	  { "info", 0, 0, 'i' },
	  { "baseboard", 0, 0, 'B' },
	  { "adc", 0, 0, 'a' },
	  { "random", 0, 0, 'x' },
	  { "watchdog", 0, 0, 'W' },
	  { "setrng", 0, 0, 'n' },
	  { "resetswitchon", 0, 0, 'X' },
	  { "resetswitchoff", 0, 0, 'Y' },
	  { "loadfpga", 1, 0, 'l' },
	  { "loadsp6", 0, 0, 'L' },
	  { "loadlm32", 1, 0, 'M' },
	  { "cputemp", 0, 0, 'q' },
	  { "removejp", 1, 0, 'U' },
	  { "setjp", 1, 0, 'J' },
	  { "touchon", 0, 0, 'r' },
	  { "touchoff", 0, 0, 'T' },
	  { "bbclkon", 0, 0, 'j' },
	  { "bbclkoff", 0, 0, 'H' },
	  { "bbclk2on", 0, 0, 'E' },
	  { "bbclk3on", 0, 0, 'I' },
	  { "txenon", 1, 0, 'k' },
	  { "txenoff", 1, 0, 'K' },
	  { "canon", 1, 0, 'N' },
	  { "canoff", 1, 0, 'f' },
	  { "ethvlan", 0, 0, 'P' },
	  { "ethswitch", 0, 0, 'y' },
	  { "ethinfo", 0, 0, 'C' },
	  { "ethwlan", 0, 0, 'o' },
	  { "ethbb", 0, 0, 'm' },
	  { 0, 0, 0, 0 }
	};

	if(argc == 1) {
		usage(argv);
		return(1);
	}

	while((c = getopt_long(argc, argv,
	  "hgs:RtSFpiBaebcdD:O:G:xWXYl:n:qU:J:vrTHk:K:N:f:PyEICLlmM:o",
          long_options, NULL)) != -1) {
		switch (c) {
		case 'v':
			opt_nvram = 1;
			break;
		case 'n':
			opt_setrng = 1;
			break;
		case 'l':
			opt_loadfpga = strdup(optarg);
			break;
		case 'L':
			opt_loadsp6 = 1;
			break;
		case 'M':
			opt_lm32 = strdup(optarg);
			if (strcmp("-", optarg) == 0) ifile = stdin;
			else ifile = fopen(opt_lm32, "r");
			if (!ifile) {
				perror(optarg);
				return 3;
			}
			break;
		case 'X':
			opt_resetswitchon = 1;
			break;
		case 'Y':
			opt_resetswitchoff = 1;
			break;
		case 'W':
			opt_watchdog = 1;
			break;
		case 'D':
			opt_setdio = 1;
			setdio = strdup(optarg);
			break;
		case 'O':
			opt_clrdio = 1;
			clrdio = strdup(optarg);
			break;
		case 'G':
			opt_getdio = 1;
			getdio = strdup(optarg);
			break;
		case 'e':
			opt_greenledon = 1;
			break;
		case 'b':
			opt_greenledoff = 1;
			break;
		case 'c':
			opt_redledon = 1;
			break;
		case 'd':
			opt_redledoff = 1;
			break;
		case 'i':
			opt_info = 1;
			opt_baseboard = 1;
			break;
		case 'B':
			opt_baseboard = 1;
			break;
		case 'a':
			opt_adc = 1;
			opt_baseboard = 1;
			break;
		case 'S':
			opt_setrtc = 1;
			break;
		case 'F':
		case 'p':
			opt_rtctemp = 1;
			break;
		case 'g':
			opt_getmac = 1;
			break;
		case 's':
			opt_setmac = 1;
			opt_mac = strdup(optarg);
			break;
		case 'R':
			opt_reboot = 1;
			break;
		case 't':
			opt_getrtc = 1;
			break;
		case 'x':
			opt_random = 1;
			break;
		case 'U':
			opt_removejp = strtoull(optarg, NULL, 0);
			break;
		case 'J':
			opt_setjp = strtoull(optarg, NULL, 0);
			break;
		case 'q':
			opt_cputemp = 1;
			break;
		case 'r':
			opt_touchon = 1;
			opt_baseboard = 1;
			break;
		case 'T':
			opt_touchoff = 1;
			break;
		case 'j':
			opt_bbclkon = 1;
			break;
		case 'H':
			opt_bbclkoff = 1;
			break;
		case 'E':
			opt_bbclk2on = 1;
			break;
		case 'I':
			opt_bbclkon = 1;
			opt_bbclk2on = 1;
			break;
		case 'k':
			opt_txenon = 1;
			txen_on = strdup(optarg);
			break;
		case 'K':
			opt_txenoff = 1;
			txen_off = strdup(optarg);
			break;
		case 'N':
			opt_canon = 1;
			can_on = strdup(optarg);
			break;
		case 'f':
			opt_canoff = 1;
			can_off = strdup(optarg);
			break;
		case 'P':
			opt_ethvlan = 1;
			break;
		case 'y':
			opt_ethswitch = 1;
			break;
		case 'C':
			opt_ethinfo = 1;
			break;
		case 'm':
			onboardsw = 0;
			break;
		case 'o':
			opt_ethwlan = 1;
			break;
		case 'h':
		default:
			usage(argv);
			return(1);
		}
	}

	if (opt_loadfpga) {
		int x, h;
		//unsigned int prev_gpioa_en, prev_gpioa_ddr;
		//signed char x;
		const char * ispvmerr[] = { "pass", "verification fail",
		  "can't find the file", "wrong file type", "file error",
		  "option error", "crc verification error" };

		// Lets not send a lattice bitstream to a Xilinx JTAG port.
		assert(0x4740 != model);
		h = open("/proc/irq/80/irq", O_RDONLY);
		if (h < 0) {
			fprintf(stderr, "Can't program the fpga because /proc/irq/80/irq "
				"can't be found.\nYou must upgrade your kernel\n");
		}
		assert(h > 0);
		x = ispVM(opt_loadfpga);
		close(h);

		if (x == 0) {
			printf("loadfpga_ok=1\n");
		} else {
			assert(x < 0);
			printf("loadfpga_ok=0\n");
			printf("loadfpga_error=\"%s\"\n", ispvmerr[-x]);
		}
	}

	if ((model == 0x4740) && opt_loadsp6) {
		int h;
		h = open("/proc/irq/80/irq", O_RDONLY);
		if (h < 0) {
			fprintf(stderr, "Can't program the fpga because /proc/irq/80/irq "
				"can't be found.\nYou must upgrade your kernel\n");
		}
		assert(h > 0);

		poke16(0x48, 0x7);
		poke16(0x46, bitstream[0]);
 
		for (i = 0; i < (NWORDS-1); i++) {
			poke16(0x46, bitstream[i]);
			poke16(0x48, 0x1);
			poke16(0x48, 0x0);
			poke16(0x48, 0x1);
		}
		poke16(0x46, bitstream[i]);
		for (i = 0; i < 16; i++) {
			poke16(0x48, 0x7);
			poke16(0x48, 0x6);
		}
		// MFP 18 is the FPGA DONE signal
        while (!(mvgpioregs[0] & (1 << 18))) {};

		close(h);
	}

	if ((model == 0x4740) && opt_lm32) {
		volatile char *bufpg;
		char *buf;
		unsigned short t1, t2;
		int ret, i;
		char s[20];


		buf = malloc(0x9000);
		bufpg = (unsigned short *)(((unsigned int)buf + 0xfff) & ~0xfff);
		mvdmainit(2);

		t1 = peek16(0x8);
		ret = fread((char *)bufpg, 1, 0x8000, ifile);
		if (ret & 0xff) ret = (ret + 0x100) & 0xffffff00;
		poke16(0x4a, 0x1000); // put LM32 in reset
		mvdmastream(0x10000, bufpg, ret, 1, 1);
		while(peek16(0x4a) & 0x7ff) peek16(0x4c); // empty FIFO
		t2 = peek16(0x8);
		poke16(0x4a, 0x0); // take LM32 out of reset
		printf("lm32_load_usec=%d\n", t2 - t1);

		// LM32 now sends us the usec reading:
		do {
			while ((peek16(0x4a) & 0x7ff) == 0) {};
			c = peek16(0x4c);
			s[i] = c;
			i++;
		} while (c != ' ');
		s[i] = NULL;
		t2 = strtoul(s, NULL, 0);
		printf("lm32_init_usec=%d\n\n", t2 - t1);

		printf("Monitoring LM32 console:\n");
		while (1) {
			i = peek16(0x4a);
			if ((i & 0x7ff) == 0) usleep(1);
			else {
				i = i & 0x7ff;
				while(i--) putchar(peek16(0x4c));
				fflush(stdout);
			}
		}
    //7'h4a: wb_dat = {3'd0, lm32_rst, fifo_full, fifo_count};
    //7'h4c: wb_dat = {8'd0, fifo_dout};

	}

	if (opt_random) printf("random=0x%x%x\n", peek16(0x0e), peek16(0x0c));
	if (opt_setrng) {
		FILE *urandom = NULL;
		unsigned int rng;
		rng = peek16(0x0e) << 16;
		rng |= peek16(0xc);

		urandom = fopen("/dev/urandom", "w");
		assert(urandom != NULL);
		fwrite(&rng, 2, 1, urandom);
		fclose(urandom);
	}

	if (opt_resetswitchon) {
		unsigned short x;
		x = peek16(0x2);
		x |= 0x8000;
		poke16(0x2, x);
	} else if (opt_resetswitchoff) {
		unsigned short x;
		x = peek16(0x2);
		x &= ~0x8000;
		poke16(0x2, x);
	}

	if (opt_info) {
		unsigned short x;
		unsigned int pclk = 0, dclk = 0;
		printf("model=0x%x\n", peek16(0x00));
		x = peek16(0x02);
		printf("submodel=0x%x\n", peek16(0x2a));
		printf("revision=0x%x\n", x & 0xf);
		printf("bootmode=0x%x\n", (x >> 4) & 0x3);
		printf("bootdev=0x%x\n", (x >> 6) & 0x3);
		printf("fpga_pn=");
		slock();
		if (model == 0x4740) {
			printf("XC6SLX25T\n");
		} else switch (read_tagmem_id()) {
			case 0xc2099480:
				printf("LFXP25E\n");
				break;
			case 0xc2059480:
				printf("LFXP28E\n");
				break;
			default:
				printf("unknown\n");
		}

		read_tagmem(tagmem);
		sunlock();

		//set mfp_121 to an input and read if high (pxa168) or low (pxa168)
		mvgpioregs[0x10c/4] &= ~(1 << 25); 
		if(mvgpioregs[0x100/4] & (1 << 25)) 
			printf("processor=pxa166\n");
		else 
			printf("processor=pxa168\n");

      calculateClocks(&pclk, &dclk);
      printf("pclk=%d\ndclk=%d\n", pclk, dclk);
	}


	if((model == 0x7700 || model == 0x7250) &&
	 (opt_getdio || opt_clrdio || opt_setdio)) {
	 	evgpioinit();
	 	if(opt_setdio) {
	 		int j = 0, sz = strlen(setdio);
			char *st = 0, *en = 0;
			for(st = setdio; j < sz; st = en + 1) {
				int dio = strtoul(st, &en, 0);
				j += (en - st) + 1;
				evsetddr(dio, 1);
				evsetdata(dio, 1);
			}
	 	} 
	 	if(opt_clrdio) {
	 		int j = 0, sz = strlen(clrdio);
			char *st = 0, *en = 0;
			for(st = clrdio; j < sz; st = en + 1) {
				int dio = strtoul(st, &en, 0);
				j += (en - st) + 1;
				evsetddr(dio, 1);
				evsetdata(dio, 0);
			}
	 	}
	 	if(opt_getdio) {
	 		int j = 0, sz = strlen(getdio);
			char *st = 0, *en = 0;
			for(st = getdio; j < sz; st = en + 1) {
				int dio = strtoul(st, &en, 0);
				j += (en - st) + 1;
				evsetddr(dio, 0);
				printf("dio%d=%d\n", dio, evgetin(dio));
			}
	 	}
	} else if (opt_getdio || opt_clrdio || opt_setdio) {
		uint16_t ddr1, ddr2, ddr3, ddr4;
		ddr1 = peek16(0x18);
		ddr2 = peek16(0x1a);
		ddr3 = peek16(0x1c);
		ddr4 = peek16(0x1e);

		if(opt_setdio || opt_clrdio) {
			uint16_t data1, data2, data3, data4;
			data1 = peek16(0x10);
			data2 = peek16(0x12);
			data3 = peek16(0x14);
			data4 = peek16(0x16);
			if(opt_setdio) {
				int j = 0, sz = strlen(setdio);
				char *st = 0, *en = 0;
				for(st = setdio; j < sz; st = en + 1) {
					int dio = strtoul(st, &en, 0);
					j += (en - st) + 1;
					if(dio < 15) {
						data1 |= (1 << dio); 
						ddr1 |= (1 << dio);
					} else if (dio < 21) {
						data2 |= (1 << (dio - 15));
						ddr2 |= (1 << (dio - 15));
					} else if (dio < 27) {
						data2 |= (1 << (dio - 16));
						ddr2 |= (1 << (dio - 16));
					} else if (dio < 43) {
						data3 |= (1 << (dio - 27));
						ddr3 |= ( 1 << (dio - 27));
					} else if (dio < 60) {
						data4 |= (1 << (dio - 48));
						ddr4 |= (1 << (dio - 48));
					}
				}
			}

			if(opt_clrdio) {
				int j = 0, sz = strlen(clrdio);
				char *st = 0, *en = 0;
				for(st = clrdio; j < sz; st = en + 1) {
					int dio = strtoul(st, &en, 0);
					j += (en - st) + 1;
					if(dio < 15) {
						data1 &= ~(1 << dio); 
						ddr1 |= (1 << dio);
					} else if (dio < 21) {
						data2 &= ~(1 << (dio - 15));
						ddr2 |= (1 << (dio - 15));
					} else if (dio < 27) {
						data2 &= ~(1 << (dio - 16));
						ddr2 |= (1 << (dio - 16));
					} else if (dio < 43) {
						data3 &= ~(1 << (dio - 27));
						ddr3 |= ( 1 << (dio - 27));
					} else if (dio < 60) {
						data4 &= ~(1 << (dio - 48));
						ddr4 |= (1 << (dio - 48));
					}
				}
			}

			poke16(0x10, data1);
			poke16(0x12, data2);
			poke16(0x14, data3);
			poke16(0x16, data4);
			poke16(0x18, ddr1);
			poke16(0x1a, ddr2);
			poke16(0x1c, ddr3);
			poke16(0x1e, ddr4);
		}

		if(opt_getdio) {
			int j = 0, sz = strlen(getdio);
			char *st = 0, *en = 0;
			for(st = getdio; j < sz; st = en + 1) {
				int dio = strtoul(st, &en, 0);
				int value = -1;
				j += (en - st) + 1;
				if(dio < 15) {
					ddr1 &= ~(1 << dio);
					poke16(0x18, ddr1);
					value = peek16(0x20) & (1 << dio);
				} else if (dio < 21) {
					ddr2 &= ~(1 << (dio - 15));
					poke16(0x18, ddr2);
					value = peek16(0x22) & (1 << (dio - 15));
				} else if (dio < 27) {
					ddr2 &= ~(1 << (dio - 16));
					poke16(0x18, ddr2);
					value = peek16(0x22) & (1 << (dio - 16));
				} else if (dio < 43) {
					ddr3 &= ~(1 << (dio - 27));
					poke16(0x1c, ddr3);
					value = peek16(0x24) & (1 << (dio - 27));
				} else if (dio < 60) {
					ddr4 &= ~(1 << (dio - 48));
					poke16(0x1e, ddr4);
					value = peek16(0x26) & (1 << (dio - 48));
				}

				printf("dio%d=%d\n", dio, value ? 1 : 0);
			}
		}
	}

	if (opt_baseboard && model != 0x7700) {
		unsigned short prev1, prev2, prev3, prev4;
		unsigned int i, x, rev;
		int bb_fd = 0;
		char buf[5] = {0};

		bb_fd = open("/dev/tsbaseboardid", O_RDONLY);
		if(bb_fd > 0) {
			read(bb_fd, &buf, 4);
			buf[4] = '\0';
			baseboard = strtoul(buf, NULL, 0);
		} else {
			prev1 = peek16(0x4);
			prev2 = peek16(0x12);
			prev3 = peek16(0x1a);
			prev4 = peek16(0x10);
			poke16(0x4, 0); // disable muxbus
			poke16(0x10, prev4 & ~0x20);
			poke16(0x1a, prev3 | 0x200);
			for(i=0; i<8; i++) {
				x = prev2 & ~0x1a00;
				if (!(i & 1)) x |= 0x1000;
				if (!(i & 2)) x |= 0x0800;
				if (i & 4) x |= 0x0200;
				poke16(0x12, x);
				usleep(1);
				baseboard = (baseboard >> 1);
				if (peek16(0x20) & 0x20) baseboard |= 0x80;
			}
			poke16(0x4, prev1);
			poke16(0x12, prev2);
			poke16(0x1a, prev3);
			poke16(0x10, prev4);
		}
		rev = (baseboard & 0xc0) >> 6;
		baseboard &= ~0xc0;
		printf("baseboard_model=0x%x\n", baseboard);
		switch (baseboard & ~0xc0) {
			case 0:
				printf("baseboard=8200\n");
				break;
			case 2:
				printf("baseboard=8390\n");
				break;
			case 4:
				printf("baseboard=8500\n");
				break;
			case 5:
				printf("baseboard=8400\n");
				break;
			case 6:
				printf("baseboard=8160\n");
				break;
			case 7:
				printf("baseboard=8100\n");
				break;
			case 8:
				printf("baseboard=8820\n");
				break;
			case 9:
				printf("baseboard=8150\n");
				break;
			case 10:
				printf("baseboard=8900\n");
				break;
			case 11:
				printf("baseboard=8290\n");
				break;
			case 13:
				printf("baseboard=8700\n");
				break;
			case 14:
				printf("baseboard=8280\n");
				break;
			case 15:
				printf("baseboard=8380\n");
				break;
			case 16:
				printf("baseboard=an20\n");
				break;
			case 17:
				printf("baseboard=8920\n");
				break;
			case 19:
				printf("baseboard=8550\n");
				break;
		}
		printf("baseboard_rev=%c\n", rev + 65);
	}

	if (opt_adc && model != 0x7250) {
		int x;
		// configure ADC:
		switch (baseboard) {
			case 2:
				poke16(0x400, 0x28);
				break;
			case 6:
			case 7:
				poke16(0x400, 0x18);
				break;
			default: // if unknown baseboard, uses no an_sel
				// but assumes ADC is there
				poke16(0x400, 0x08);
				break;
		}
		poke16(0x402, 0x3f); // enable all 6 channels
		usleep(500000); // allow time for conversions
		for (i = 1; i <= 6; i++) {
			x = (signed short)peek16(0x402 + 2*i);
			if (i > 2) x = (x * 1006)/200;
			x = (x * 2048)/0x8000;
			printf("adc%d=%d\n", i, x);
		}
	} else if (opt_adc) {
		int i, j, k, d;
		unsigned short ad, adreg;
		char ack;

		adreg = peek16(0x34);
		adreg &= ~0x70;
		adreg |= 0x4c; /* start at mux position 4 and loop down to 0 */
#define dly() do { for (d = 0; d < 3; d++) peek16(0); } while(0)
#define sda(x) do { if ((x)) adreg |= 4; else adreg &= ~4; poke16(0x34, adreg); } while(0)
#define scl(x) do { if ((x)) adreg |= 8; else adreg &= ~8; poke16(0x34, adreg); } while(0)

adc_i2c_start:
		scl(1); sda(0); dly(); /* I2C start */

		/* Sending 0x68 address with R/W bit low, MSB first */
		for (i = 0, c = ((0x68 << 1) | 0); i < 8; i++, c = c << 1) {
			scl(0);
			if (c & 0x80) sda(1); else sda(0);
			dly();
			scl(1);
			dly();
		}

#define sda_i(x) peek16(0x34) & 1
		scl(0); sda(1); dly(); scl(1); ack = sda_i()?0:1; dly(); /* check ack */

		/* Sending 0x88, 16-bit, one-shot sampling, no gain */
		for (i = 0, c = 0x88; i < 8; i++, c = c << 1) {
			scl(0);
			if (c & 0x80) sda(1); else sda(0);
			dly();
			scl(1);
			dly();
		}

		scl(0); sda(1); dly(); scl(1); ack = sda_i()?0:1; dly(); /* check ack */
		scl(0); sda(0); dly(); scl(1); dly(); sda(1); /* I2c stop */

		scl(1); sda(0); dly(); /* I2C start */

		/* Sending 0x68 address with R/W bit high, MSB first */
		for (i = 0, c = ((0x68 << 1) | 1); i < 8; i++, c = c << 1) {
			scl(0);
			if (c & 0x80) sda(1); else sda(0);
			dly();
			scl(1);
			dly();
		}

		scl(0); sda(1); dly(); scl(1); ack = sda_i()?0:1; dly(); /* check ack */

		k = 2; 
		for (j = 0; j < k; j++) { 
			for (i = 0; i < 8; i++) {
				scl(0);
				if (i == 0) sda(1);
				dly();
				scl(1);
				dly();
			}

			/* Assert ack inbetween bytes */
			scl(0); sda(0); dly(); scl(1); dly(); /* Assert ACK */
		}

adc_i2c_retry:
		for (c = i = 0; i < 8; i++) {
			scl(0);
			if (i == 0) sda(1);
			dly();
			scl(1);
			c <<= 1;
			if (sda_i()) c |= 1;
			dly();
		}

		if (c & 0x80) { /* ADC sample not done ? */
			scl(0); sda(0); dly(); scl(1); dly(); /* Assert ACK */
			goto adc_i2c_retry;
		}

		scl(0); dly(); scl(1); dly(); /* Don't assert ACK */

		scl(0); sda(0); dly(); scl(1); dly(); sda(1); /* I2c stop */

		/* Now read real sample */
		scl(1); sda(0); dly(); /* I2C start */

		/* Sending 0x68 address with R/W bit high, MSB first */
		for (i = 0, c = ((0x68 << 1) | 1); i < 8; i++, c = c << 1) {
			scl(0);
			if (c & 0x80) sda(1); else sda(0);
			dly();
			scl(1);
			dly();
		}

		scl(0); sda(1); dly(); scl(1); ack = sda_i()?0:1; dly(); /* check ack */

		k = 1; 
		for (j = 0; j < k; j++) { /* Now shift data byte into ad */
			for (ad = i = 0; i < 8; i++) {
				scl(0);
				if (i == 0) sda(1);
				dly();
				scl(1);
				ad <<= 1;
				if (sda_i()) ad |= 1;
				dly();
			}

			/* Assert ack inbetween bytes */
			scl(0); sda(0); dly(); scl(1); dly(); /* Assert ACK */
		}

		for (i = 0; i < 8; i++) {
			scl(0);
			if (i == 0) sda(1);
			dly();
			scl(1);
			ad <<= 1;
			if (sda_i()) ad |= 1;
			dly();
		}
		scl(0); dly(); scl(1); dly(); /* Don't assert ACK */

		scl(0); sda(0); dly(); scl(1); dly(); sda(1); /* I2c stop */

		printf("adc%d=%d\n", (adreg>>4) & 7, ad);

		if ((adreg>>4) & 7) {
			adreg -= 0x10;
			goto adc_i2c_start;
		}

	}

	if (opt_getmac) {
		unsigned char a, b, c;
		slock();
		read_tagmem(tagmem);
		sunlock();
		a = tagmem[0] >> 24;
		b = tagmem[0] >> 16;
		c = tagmem[0] >> 8;
		printf("mac=00:d0:69:%02x:%02x:%02x\n", a, b, c);
		printf("shortmac=%02x%02x%02x\n", a, b, c);
		printf("model=0x%x\n", peek16(0x00));
		for (i = 0; i < 8; i++) printf("jp%d=%c\n", i + 1, ((tagmem[0] & (1<<i)) ? '0' : '1'));
	}

	if (opt_setmac) {
		unsigned int origtag[24];
		unsigned int a, b, c;
		int r;
		r = sscanf(opt_mac, "%*x:%*x:%*x:%x:%x:%x",  &a, &b, &c);
		assert(r == 3); /* XXX: user arg problem */
		assert(a < 0x100);
		assert(b < 0x100);
		assert(c < 0x100);
		slock();
		read_tagmem(origtag);
		memcpy(tagmem, origtag, 24 * sizeof (int));
		tagmem[0] &= ~0xffffff00;
		tagmem[0] |= (a<<24|b<<16|c<<8);
		if (a == 0xff && b == 0xff && c == 0xff) {
			for (i = 1; i < 24; i++) tagmem[i] = 0xffffffff;
			tagmem[0] |= (0x3f<<1); /* unset jp 2-7 */
			tagmem[0] &= ~(1<<7); /* set jp 8 */
		}
		
		if(memcmp(origtag, tagmem, 24 * sizeof (int))) {	
			erase_tagmem();
			write_tagmem(tagmem);
		}
		sunlock();
	}

	if (opt_setjp || opt_removejp) {
		unsigned int origtag[24];

		slock();
		read_tagmem(origtag);
		memcpy(tagmem, origtag, 24 * sizeof (int));
		if (opt_setjp) tagmem[0] &= ~(1<<((opt_setjp-1)&7));
		else tagmem[0] |= (1<<((opt_removejp-1)&7));

		if(memcmp(origtag, tagmem, 24 * sizeof (int))) {
			erase_tagmem();
			write_tagmem(tagmem);
		}
		sunlock();
	}

	if (opt_cputemp) {
		int data_out, cal1, cal2, t, offset;
		volatile unsigned long *tsc_config;
		volatile unsigned long *tsc_cal;

		tsc_config = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED, devmem, 0xD4019000);
		assert (tsc_config != MAP_FAILED);
		tsc_cal = (unsigned long *)((unsigned char *)pmua_base + 0x2000);
		tsc_config[0x1FF] = 0x10000;
		while (! (tsc_config[0x1FF] & 0x400)) usleep(1000);
		data_out = tsc_config[0x1FF] & 0x3FF;
		cal1 = tsc_cal[0x320];
		cal2 = tsc_cal[0x322];
		offset = ((cal1 >> 15) & 1) | ((cal1 >> 16) & 2) | ((cal2 >> 16) & 4) | ((cal2 << 3) & 0x80000000);
		t = (((data_out * 1000) - 525900) / 1988) + offset;
		printf("cputemp_millicelsius=%d\n", t*1000);
		munmap((void*)tsc_config, getpagesize());
	}

	if (opt_reboot) {
		struct sched_param sched;
		sync();
		poke16(0x06, 0);
		sched.sched_priority = 99;
		sched_setscheduler(0, SCHED_FIFO, &sched);
		while(1);
	}

	if (opt_ethswitch || opt_ethinfo || opt_ethvlan || opt_ethwlan) {
		assert(model != 0x7700);
		if(phy_read(0x18, 0x03, &swmod) == -1) {
			printf("switch_model=none\n");
			opt_ethvlan = 0;
			opt_ethwlan = 0;
			opt_ethswitch = 0;
			opt_ethinfo = 0;
		} else {
			swmod &= 0xFFF0;
			if(swmod == 512){
				printf("switch_model=88E6020\n");
			}else if (swmod == 1792) {
				printf("switch_model=88E6070\n");
			} else if(swmod == 1808) {
				printf("switch_model=88E6071\n");
				fprintf(stderr, "Unsupported switch\n");
			} else if(swmod == 8704) {
				printf("switch_model=88E6220\n");
				fprintf(stderr, "Unsupported switch\n");
			} else if(swmod == 9472) {
				printf("switch_model=88E6251\n");
				fprintf(stderr, "Unsupported switch\n");
			} else {
				printf("switch_model=unknown\n");
			}
		}
	}

	if (opt_ethswitch || opt_ethvlan || opt_ethwlan) {
		if(swmod == 512) { // Marvell 88E6020 Network Switch
			// this chip has 2 PHYs and 2 RMII ports 
			// Apply Marvell 88E60x0 Errata 3.1 for PHY 0 and 1.
			switch_errata_3_1(0);
			switch_errata_3_1(1<<5);
			// enable both PHYs
			switch_enphy(0x9600);
			switch_enphy(0x9620);

			// enable port flood on all ports
			if(opt_ethswitch) {
				switch_enflood(0x18);
				switch_enflood(0x19);
				switch_enflood(0x1d);
			} else if (opt_ethvlan || opt_ethwlan) {
				switch_enbcastflood(0x18);
				switch_enbcastflood(0x19);
				switch_enbcastflood(0x1d);
			}

			// Force link up on P5, the CPU port
			switch_forcelink(0x1d);

			// Set CPUDest so we know where to send mgmt frames
			switch_cpudest(5);

			// With this chip ethvlan and wlan switch are the same since 
			// we only have 2 ports
			if(opt_ethvlan || opt_ethwlan) {
				struct vtu entry;
				// Configure P5
				phy_write(0x1d, 0x05, 0x0);
				phy_write(0x1d, 0x06, 0x5f);
				phy_write(0x1d, 0x07, 0x1);
				phy_write(0x1d, 0x08, 0x480);

				for(i=0; i < 7; i++) {
					entry.tags[i] = 3; // leave the port alone by default
					entry.forceVID[i] = 0;
				}

				// Set up VLAN #1 on P0 and P5 
				entry.v = 1;
				entry.vid = 1;
				entry.tags[5] = 2; // CPU port is always egress tagged
				entry.tags[0] = 1; // External Port A egress untagged
				vtu_add_entry(&entry);
				entry.tags[0] = 3; // Do not configure port A again

				// Setup VLAN #2 on P1 and P5 
				entry.vid = 2;
				entry.tags[1] = 1; // External PORT B egress untagged
				vtu_add_entry(&entry);
			}
		} else if (swmod == 1792) { // Marvell 88E6070 Network Switch
			// Used on the TS-8700
			// enable all PHYs
			
			switch_errata_3_1(0);
			switch_errata_3_1(1<<5);
			switch_errata_3_1(2<<5);
			switch_errata_3_1(3<<5);
			switch_errata_3_1(4<<5);
			
			switch_enphy(0x9600);
			switch_enphy(0x9620);
			switch_enphy(0x9640);
			switch_enphy(0x9660);
			switch_enphy(0x9680);

			switch_enflood(0x18);
			switch_enflood(0x19);
			switch_enflood(0x1a);
			switch_enflood(0x1b);
			switch_enflood(0x1c);
			if(opt_ethvlan) {
				struct vtu entry;
				for(i=0; i < 7; i++) {
					entry.tags[i] = 3; // leave the port alone by default
					entry.forceVID[i] = 0;
				}
				entry.v = 1;

				// Set up VLAN #1 on P0 and P1
				entry.vid = 1;
				entry.tags[0] = 2; // TS-4710 CPU port is always egress tagged
				entry.tags[1] = 1; // External Port A egress untagged
				vtu_add_entry(&entry);
				entry.tags[1] = 3; // Do not configure port A again

				// Setup VLAN #2 on P0 and P2
				entry.vid = 2;
				entry.tags[2] = 1; // External PORT B egress untagged
				vtu_add_entry(&entry);
				entry.tags[2] = 3; // Do not configure port B again

				// Setup VLAN #2 on P0 and P3
				entry.vid = 3;
				entry.tags[3] = 1; // External PORT C egress untagged
				vtu_add_entry(&entry);
				entry.tags[3] = 3; // Do not configure port C again

				// Setup VLAN #2 on P0 and P4
				entry.vid = 4;
				entry.tags[4] = 1; // External PORT D egress untagged
				vtu_add_entry(&entry);
			} else if (opt_ethwlan) {
				struct vtu entry;
				for(i=0; i < 7; i++) {
					entry.tags[i] = 3; // leave the port alone by default
					entry.forceVID[i] = 0;
				}
				entry.v = 1;

				// Set up VLAN #1 with P0 and P1
				entry.vid = 1;
				entry.tags[0] = 2; // TS-4710 CPU port is always egress tagged
				entry.tags[1] = 1; // External Port A egress untagged
				vtu_add_entry(&entry);
				entry.tags[1] = 3; // Do not configure port A again

				// Set up VLAN #1 with P0, P2, P3, and P4
				entry.vid = 2;
				entry.tags[0] = 2; // TS-4710 CPU port is always egress tagged
				entry.tags[2] = 1; // External Port B egress untagged
				entry.tags[3] = 1; // External Port C egress untagged
				entry.tags[4] = 1; // External Port D egress untagged
				vtu_add_entry(&entry);
			}
		} else {
			fprintf(stderr, "Switch not configured\n");
		}
	}

	if (opt_ethinfo) {
		int ports = 0, i = 0, port, vtu = 0;
		int phy[8] = {0,0,0,0,0,0,0,0};
		uint16_t x, r7;

		if(swmod == 512){ // 4712
			ports = 2;
			phy[0] = 0x18;
			phy[1] = 0x19;
		} else if (swmod == 1792) { // 8700
			ports = 4;
			phy[0] = 0x19;
			phy[1] = 0x1a;
			phy[2] = 0x1b;
			phy[3] = 0x1c;
		} else {
			printf("Unknown switch: %d\n", swmod);
			return 1;
		}

		printf("switch_ports=\"");
		for (i = 0; i < ports; i++) {
			printf("%c", 97 + i);
			if (i != ports - 1)
				printf(" ");
		}

		printf("\"\n");

		for (i = 0; i < ports; i++)
		{
			uint16_t dat;
			phy_read(phy[i], 0x0, &dat);
			printf("switchport%c_link=%d\n", 
			  97 + i,
			  dat & 0x1000 ? 1 : 0);
			printf("switchport%c_speed=", 97 + i);
			dat = (dat & 0xf00) >> 8;
			if(dat == 0x8)
				printf("10HD\n");
			else if (dat == 0x9)
				printf("100HD\n");
			else if (dat == 0xa)
				printf("10FD\n");
			else if (dat == 0xb)
				printf("100FD\n");
		}

		x = vtu_readwait(VTU_OPS_REGISTER);	
		phy_write(GLOBAL_REGS_1, VTU_VID_REG, 0xFFF);
		while(1) {
			phy_write(GLOBAL_REGS_1, VTU_OPS_REGISTER, VTU_BUSY | VTU_OP_GET_NEXT);
			vtu_readwait(VTU_OPS_REGISTER);
			phy_read(GLOBAL_REGS_1, VTU_VID_REG, &x);

			// No more VTU entries
			if ((x & 0xFFF) == 0xFFF) {
				printf("vtu_total=%d\n", vtu);
				break;
			}
			vtu++;
			printf("vtu%d_vid=%d\n", vtu, x & 0xfff);
			for (port = 0; port < 7; port++)
			{
				phy_read(0x18 + port, 7, &r7);
				if(port == 0)
					phy_read(GLOBAL_REGS_1, 0x07, &x);
				else if (port == 4)
					phy_read(GLOBAL_REGS_1, 0x08, &x);


				switch(port) {
				case 0:
					if ((x & 0x0003) != 0x0003) {
						printf("vtu%d_port0=1\n"
							   "vtu%d_port0_forcevid=%c\n"
							   "vtu%d_port0_egress=%s\n"
							   "vtu%d_port0_alias=%s\n",
							    vtu,
							    vtu, r7 & 0x1000? 'Y':'N',
							    vtu, MemberTagString(x & 3),
							    vtu, !onboardsw ? "a":"cpu");
					}
					break;
				case 1:
					if ((x & 0x0030) != 0x0030) {
						printf("vtu%d_port1=1\n"
							   "vtu%d_port1_forcevid=%c\n"
							   "vtu%d_port1_egress=%s\n"
							   "vtu%d_port1_alias=%s\n",
							    vtu,
							    vtu, r7 & 0x1000? 'Y':'N',
							    vtu, MemberTagString((x>>4)&3),
							    vtu, !onboardsw ? "b":"a");
					}
					break;
				case 2:
					if ((x & 0x0300) != 0x0300) {
						printf("vtu%d_port2=1\n"
							   "vtu%d_port2_forcevid=%c\n"
							   "vtu%d_port2_egress=%s\n"
							   "vtu%d_port2_alias=%s\n",
							    vtu,
							    vtu, r7 & 0x1000? 'Y':'N',
							    vtu, MemberTagString((x>>8)&3),
							    vtu, !onboardsw ? "unused":"b");
					}
					break;
				case 3:
					if ((x & 0x3000) != 0x3000) {
						printf("vtu%d_port3=1\n"
							   "vtu%d_port3_forcevid=%c\n"
							   "vtu%d_port3_egress=%s\n"
							   "vtu%d_port3_alias=%s\n",
							    vtu,
							    vtu, r7 & 0x1000? 'Y':'N',
							    vtu, MemberTagString((x>>12)&3),
							    vtu, !onboardsw ? "unused":"c");
					}
					break;
				case 4:
					if ((x & 0x0003) != 0x0003) {
						printf("vtu%d_port4=1\n"
							   "vtu%d_port4_forcevid=%c\n"
							   "vtu%d_port4_egress=%s\n"
							   "vtu%d_port4_alias=%s\n",
							    vtu,
							    vtu, r7 & 0x1000? 'Y':'N',
							    vtu, MemberTagString(x & 3),
							    vtu, !onboardsw ? "b":"d");
					}
					break;
				case 5:
					if ((x & 0x0030) != 0x0030) {
						printf("vtu%d_port5=1\n"
							   "vtu%d_port5_forcevid=%c\n"
							   "vtu%d_port5_egress=%s\n"
							   "vtu%d_port5_alias=%s\n",
							    vtu,
							    vtu, r7 & 0x1000? 'Y':'N',
							    vtu, MemberTagString((x>>4)&3),
							    vtu, !onboardsw ? "cpu":"unused");
					}
					break;
				case 6:
					if ((x & 0x0300) != 0x0300) {
						printf("vtu%d_port6=1\n"
							   "vtu%d_port6_forcevid=%c\n"
							   "vtu%d_port6_egress=%s\n"
							   "vtu%d_port6_alias=%s\n",
							    vtu,
							    vtu, r7 & 0x1000? 'Y':'N',
							    vtu, MemberTagString(x & 3),
							    vtu, "unused");
					}
					break;
				}

			}

		}

	}

	if (opt_greenledoff) poke16(0x12, peek16(0x12) & ~0x800);
	if (opt_greenledon) poke16(0x12, peek16(0x12) | 0x800);
	if (opt_redledoff) poke16(0x12, peek16(0x12) & ~0x1000);
	if (opt_redledon) poke16(0x12, peek16(0x12) | 0x1000);
	if (opt_bbclkoff) {
		poke16(0x2, peek16(0x2) & ~0x800);
		poke16(0x10, peek16(0x10) & ~0x8000);
	}
	if (opt_bbclkon) poke16(0x2, peek16(0x2) | 0x800);
	if (opt_bbclk2on) poke16(0x10, peek16(0x10) | 0x8000);

	if (opt_touchon) {
		if(baseboard == 15) // 8380 uses spi pins instead for touch
			poke16(0x12, peek16(0x12) | 0x2000);

		poke16(0x2, peek16(0x2) | 0x4000);
	} 
	if (opt_touchoff) {
		if(baseboard == 15) // disable alternate touch as well
			poke16(0x12, peek16(0x12) & ~0x2000);
		poke16(0x02, peek16(0x02) & ~0x4000);
	} 
	if(opt_txenon) {
		int j = 0, sz = strlen(txen_on);
		char *st = 0, *en = 0;
		for(st = txen_on; j < sz; st = en + 1) {
			int xu = strtoul(st, &en, 0);
			j += (en - st) + 1;
			if(xu == 0) poke16(0x02, peek16(0x02) | 0x1000);
			else if (xu == 1) poke16(0x16, peek16(0x16) | 0x4000);
			else if (xu == 2) poke16(0x16, peek16(0x16) | 0x8000);
			else if (xu == 3) poke16(0x16, peek16(0x16) | 0x1000);
			else if (xu == 4) poke16(0x2, peek16(0x2) | 0x2000);
			else if (xu == 5) poke16(0x16, peek16(0x16) | 0x2000);
		}
	}

	if(opt_txenoff) {
		int j = 0, sz = strlen(txen_off);
		char *st = 0, *en = 0;
		for(st = txen_off; j < sz; st = en + 1) {
			int xu = strtoul(st, &en, 0);
			j += (en - st) + 1;
			if(xu == 0) poke16(0x02, peek16(0x02) & ~0x1000);
			else if (xu == 1) poke16(0x16, peek16(0x16) & ~0x4000);
			else if (xu == 2) poke16(0x16, peek16(0x16) & ~0x8000);
			else if (xu == 3) poke16(0x16, peek16(0x16) & ~0x1000);
			else if (xu == 4) poke16(0x2, peek16(0x2) & ~0x2000);
			else if (xu == 5) poke16(0x16, peek16(0x16) & ~0x2000);
		}
	}

	if(opt_canon) {
		int j = 0, sz = strlen(can_on);
		char *st = 0, *en = 0;
		for(st = can_on; j < sz; st = en + 1) {
			int can = strtoul(st, &en, 0);
			j += (en - st) + 1;
			if(can == 0) poke16(0x02, peek16(0x02) | 0x100);
			else if(can == 1) poke16(0x02, peek16(0x02) | 0x200);
		}
	}

	if(opt_canoff) {
		int j = 0, sz = strlen(can_off);
		char *st = 0, *en = 0;
		for(st = can_off; j < sz; st = en + 1) {
			int can = strtoul(st, &en, 0);
			j += (en - st) + 1;
			if(can == 0) poke16(0x02, peek16(0x02) & ~0x100);
			else if(can == 1) poke16(0x02, peek16(0x02) & ~0x200);
		}
	}

	if (opt_setrtc || opt_getrtc || opt_rtctemp || opt_nvram) {
		mvmfpregs[0x1ec/4] = 0x4c06; /* SCL to GPIO */
		mvmfpregs[0x1f0/4] = 0x4c06; /* SDA to GPIO */
		mvgpioregs[0x02c/4] = (3 << 23); /* 0 for output register */
	}

	if (opt_rtctemp) {
		unsigned char i, rtcdat[11];

		for(i = 0; i < 2; i++) rtcdat[i] = rtc_read(0x28+i);
		rtc_done();
		printf("rtctemp_millicelsius=%d\n",
			((rtcdat[0]|(rtcdat[1]<<8))*500)-273000);
		rtcdat[0] = rtc_read(0x7);
		for(i = 1; i < 6; i++) rtcdat[i] = rtc_read(0x16+i);
		for(i = 6; i < 10; i++) rtcdat[i] = rtc_read(0x1b+i);
		rtc_done();

		printf("rtcinfo_oscillator_ok=%d\n",
			((rtcdat[0] & 0x41) ? 0 : 1));
		printf("rtcinfo_batt_low=%d\n",
			((rtcdat[0] & 4) >> 2));
		printf("rtcinfo_batt_crit=%d\n",
			((rtcdat[0] & 2) >> 1));
		printf("rtcinfo_firstpoweroff=%02x%02x%02x%02x%02x\n",
			rtcdat[5], rtcdat[4], (rtcdat[3] & 0x7f), rtcdat[2],
			rtcdat[1]);
		printf("rtcinfo_lastpoweron=%02x%02x%02x%02x%02x\n",
			rtcdat[10], rtcdat[9], (rtcdat[8] & 0x7f), rtcdat[7],
			rtcdat[6]);
		rtcdat[0] = 0x80;
	}

	if (opt_nvram) {
		unsigned int dat[32], o, x;
		unsigned char *e, *cdat = (unsigned char *)dat, var[8];

		o = rtc_i2c_7bit_adr;
		rtc_i2c_7bit_adr = 0x57; /* NVRAM uses a different I2C adr */
		for (i = 0; i < 128; i++) cdat[i] = rtc_read(i);
		for (i = 0; i < 32; i++) {
			sprintf(var, "nvram%d", i);
			e = getenv(var);
			if (e) {
				x = strtoul(e, NULL, 0);
				rtc_write(i<<2, x);
				rtc_write(1+(i<<2), x >> 8);
				rtc_write(2+(i<<2), x >> 16);
				rtc_write(3+(i<<2), x >> 24);
			}
		}
		rtc_done();
		rtc_i2c_7bit_adr = o;
		for (i = 0; i < 32; i++) printf("nvram%d=0x%x\n", i, dat[i]);
	}

	if (opt_setrtc) {
		unsigned char rtcdat[7];
		time_t now = time(NULL);
		struct tm *tm;

		rtc_write(0x8, 0x50); /* enable RTC write, disable IRQs on batter */
		rtc_write(0x9, 0);
		rtc_write(0xa, 0x24); /* turn off reseal, set trip levels */
		rtc_write(0xd, (0xc0 | (rtc_read(0xd) & 0x1f))); /* Enable TSE once per 10min */

		tm = gmtime(&now);
		rtcdat[0] = tobcd(tm->tm_sec);
		rtcdat[1] = tobcd(tm->tm_min);
		rtcdat[2] = tobcd(tm->tm_hour) | 0x80; //24 hour bit
		rtcdat[3] = tobcd(tm->tm_mday);
		rtcdat[4] = tobcd(tm->tm_mon + 1);
		assert(tm->tm_year >= 100);
		rtcdat[5] = tobcd(tm->tm_year % 100);
		rtcdat[6] = tm->tm_wday + 1;

		for (i = 0; i < 7; i++) rtc_write(i, rtcdat[i]);
		rtc_done();
	}

	if (opt_getrtc) {
		unsigned char rtcdat[8];
		time_t now;
		struct tm tm;
		struct timeval tv;

		for (i = 0; i < 8; i++) rtcdat[i] = rtc_read(i);
		rtc_done();
		printf("rtc_oscillator_ok=%d\n", rtcdat[7] & 1 ? 0 : 1);
		if (!(rtcdat[7] & 1)) {
			tm.tm_sec = frombcd(rtcdat[0] & 0x7f);
			tm.tm_min = frombcd(rtcdat[1] & 0x7f);
			tm.tm_hour = frombcd(rtcdat[2] & 0x3f);
			tm.tm_mday = frombcd(rtcdat[3] & 0x3f);
			tm.tm_mon = frombcd(rtcdat[4] & 0x1f) - 1;
			tm.tm_year = frombcd(rtcdat[5]) + 100;
			setenv("TZ", "UTC", 1);
			now = mktime(&tm);
			tv.tv_sec = now;
			tv.tv_usec = 0;
			settimeofday(&tv, NULL);
		}
	}
	if (opt_setrtc || opt_getrtc || opt_rtctemp || opt_nvram) {
		mvmfpregs[0x1ec/4] = 0x4c00; /* SCL back to I2C */
		mvmfpregs[0x1f0/4] = 0x4c00; /* SDA back to I2C */
	}

	/* Should be last! */
	if (opt_watchdog) {
		struct sched_param sched;
		int fd, r, grain = 0, mode, t;
		char c, time = 3;
		unsigned short x = 0;
		fd_set rfds;
		struct timeval tv_now, tv_wait, tv_exp;

#define FEED0 338000
#define FEED1 2706000
#define FEED2 10824000
		/* Single character writes to /dev/watchdog keep it fed */
		/* XXX: DO NOT RUN MULTIPLE INSTANCES OF THIS! IT WILL 
		 * RESULT IN UNDEFINED BEHAVIOR!
		 */
		/* New /dev/watchdog interface:
		 * - Still supports the standard interface that expects
		 *   '0' '1' '2' or '3'.
		 * - Most other characters are still treated as 3.
		 * - 'f' (for feed) is a special character used to create
		 *   automatic feeds.
		 * - 'f' is expected to be followed by three digits, eg "f100".
		 * - Those digits are interpreted as a 3 digit number, which
		 *   is then divided by 10.  For example, "f456" feeds the
		 *   watchdog for 45.6 seconds.
		 * - 'a' (for autofeed) is a special character used to 
		 *   create an autofeed situation
		 * - In order to guarantee timeout, 'a' is expected to be
		 *   followed by a single number, 0 through 3, eg "a1"
		 * - This single number is the standard WDT interface
		 *   and is not real world time
		 * - An extended feed is canceled by any new write to
		 *   /dev/watchdog.
		 * - If 'f' is not followed by 3 digits, the behavior is
		 *   undefined, but in practice you typically just fed the
		 *   wdt for a long time.
		 * - If 'a' is not followed by a single digit, the behavior 
		 *   is undefined.
		 */

		/* We need to make sure we create the pipe before daemonizing
		 * so that as soon as the call to tshwctl -W completes we can
		 * write to the pipe.  The write side of the pipe NEEDS to be 
		 * opened with O_WRONLY and !O_NONBLOCK in order to ensure
		 * that any immediate writes will block until we open the 
		 * pipe here 
		 *
		 * bash redirects open the file with O_WRONLY and !O_NONBLOCK
		 */
		mknod("/dev/watchdog", S_IFIFO|0666, 0);
		daemon(0, 0);
		sched.sched_priority = 98;
		sched_setscheduler(0, SCHED_FIFO, &sched);
		mode = 0; // mode 1 == processing a multi-feed write
		while(1) {
			fd = open("/dev/watchdog", O_RDONLY | O_NONBLOCK);
			assert (fd != -1);
			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);
			if (mode)
				select(fd + 1, &rfds, NULL, NULL, &tv_wait);
			else
				select(fd + 1, &rfds, NULL, NULL, NULL);
			r = 0;
			if (FD_ISSET(fd, &rfds)) r = read(fd, &c, 1);
			if (r) { // data coming in /dev/watchdog
				if (c == 'f') { // new style feed
					read(fd, &c, 1);
					t = (c - '0') * 100;
					read(fd, &c, 1);
					t += (c - '0') * 10;
					read(fd, &c, 1);
					t += (c - '0');
					t = t * 100000;
					x = 0;
					if (t > FEED1) x = 1;
					if (t > FEED2) x = 2;
					poke16(0x6, x);
					switch (x) {
						case 0: grain = FEED0; break;
						case 1: grain = FEED1; break;
						case 2: grain = FEED2; break;
					}
					mode = 1;
					gettimeofday(&tv_exp, NULL);
					t += tv_exp.tv_usec;
					tv_exp.tv_usec = t % 1000000;
					tv_exp.tv_sec += t / 1000000;
				/*XXX: This is needed because close() is not
				 * guaranteed to have closed and flushed the
				 * pipe by the time we re-open and read again
				 */ 
				} else if(c == '\n') { 
				} else { // old style feed
					mode = 0;
					if(c == 'a') {
						mode = 2;
						read(fd, &c, 1);
					}
					if (c > '3' || c < '0') c = '3';
					poke16(0x6, c - '0');
					if(c == '3') mode = 0;
					/* See XXX: above */
					time = (c - '0');
				}
			}
			if (mode == 1) {
				// if in mode 1, we feed wdt and calculate
				// how long to sleep
				poke16(0x6, x);
				gettimeofday(&tv_now, NULL);
				t = tv_exp.tv_sec - tv_now.tv_sec;
				t *= 1000000;
				t += tv_exp.tv_usec;
				t -= tv_now.tv_usec;
				if (t <= grain) mode = 0;
				else if (t <= 3*grain/2) t -= grain;
				else t = grain/2;
				tv_wait.tv_sec = t / 1000000;
				tv_wait.tv_usec = t % 1000000;
			}
			if(mode == 2) {
				poke16(0x6, time);
				switch (time) {
					case 0: 
					  tv_wait.tv_sec = (FEED0 / 2) / 1000000;
					  tv_wait.tv_usec = (FEED0 / 2) % 1000000;
					  break;
					case 1:
					  tv_wait.tv_sec = (FEED1 / 2) / 1000000;
					  tv_wait.tv_usec = (FEED1 / 2) % 1000000;
					  break;
					case 2:
					  tv_wait.tv_sec = (FEED2 / 2) / 1000000;
					  tv_wait.tv_usec = (FEED2 / 2) % 1000000;
					  break;
				}
			}
			// if mode was 0 and no character was read, don't do
			// anything, just go back to waiting for a character
			close(fd);
		} // end forever loop
	}

	return 0;
}

#define MPMU_BASE mpmu_base
#define MPMU_FCCR       (*((volatile unsigned int *)(MPMU_BASE + 0x08)))
#define MPMU_POSR       (*((volatile unsigned int *)(MPMU_BASE + 0x10)))
#define MPMU_PLL1REG1   (*((volatile unsigned int *)(MPMU_BASE + 0x50)))
#define MPMU_PLL2REG1   (*((volatile unsigned int *)(MPMU_BASE + 0x60)))

#define PMUA_BASE (0x2800+pmua_base)
#define APMU_CCR        (*((volatile unsigned int *)(PMUA_BASE + 0x4)))

static void calculateClocks(unsigned int *pclk, unsigned int *dclk)
{
   unsigned int vco_ref_div,
      vco_fb_div,
      vco_div_sel_diff,
      vco_div_sel_se,
      core_clk_div,
      ddr_clk_div,
      mpmu_fccr;

   unsigned int pll1_out, pll2_out, core_clk, ddr_clk;

   mpmu_fccr = MPMU_FCCR;
   MPMU_FCCR = mpmu_fccr | (1 << 14);       // Set PLL1CEN so that we can read PLL1 status

   switch ((MPMU_FCCR >> 29) & 3)
   {
      case 2:
      {
         //ser_puts("Core and DDR2 are using PLL2\n\r");

         vco_ref_div = (MPMU_POSR >> 23) & 0x1F;

         if (vco_ref_div != 1)
            fprintf(stderr, "Error:  APMU_CCR::PLL2REFD must be set to 1\n\r");
         else
         {
            vco_fb_div = (MPMU_POSR >> 14) & 0x1FF;
            vco_div_sel_diff = (MPMU_PLL2REG1 >> 19) & 0xF;
            vco_div_sel_se = (MPMU_PLL2REG1 >> 23) & 0xF;
            core_clk_div = APMU_CCR & 3;
            ddr_clk_div = (APMU_CCR >> 12) & 3;

            pll2_out = (26000 * vco_fb_div / 3) * 1000;

            core_clk = pll2_out / (core_clk_div + 1);

            if (vco_div_sel_se == 1)
               core_clk = core_clk * 2 / 3;
            else if (vco_div_sel_se == 2)
               core_clk >>= 1;

            ddr_clk = pll2_out / (2 * (ddr_clk_div+1));

            if (vco_div_sel_diff == 1)
               ddr_clk = ddr_clk * 2 / 3;    // divide by 1.5
            else if (vco_div_sel_diff == 2)
               ddr_clk >>= 1;                // divide by 2
            else if (vco_div_sel_diff == 3)
               ddr_clk = ddr_clk * 2 / 5;    // divide by 2.5
            else if (vco_div_sel_diff == 4)
               ddr_clk = ddr_clk * 2 / 6;    // divide by 3

            *pclk = core_clk / 1000000;
            *dclk = ddr_clk / 1000000;
         }
      }
      break;
      case 1:
      {
         //ser_puts("Core and DDR2 are using PLL1 @ 624MHz\n\r");

         pll1_out = 624000000;

         vco_div_sel_se = (MPMU_PLL1REG1 >> 23) & 0xF;

         core_clk_div = APMU_CCR & 3;

         ddr_clk_div = (APMU_CCR >> 12) & 3;

         core_clk = pll1_out / (core_clk_div + 1);
         if (vco_div_sel_se == 1)
            core_clk = core_clk * 2 / 3;
         else if (vco_div_sel_se == 2)
            core_clk >>= 1;

         ddr_clk = pll1_out / (2 * (ddr_clk_div+1));

         if (vco_div_sel_se == 1)
            ddr_clk = ddr_clk * 2 / 3;
         else if (vco_div_sel_se == 2)
               ddr_clk >>= 1;

         *pclk = core_clk / 1000000;
         *dclk = ddr_clk / 1000000;
      }

      break;

      case 0:
      {
         //ser_puts("Core and DDR2 are using PLL1 @ 312MHz\n\r");

         pll1_out = 312000000;

         vco_div_sel_se = (MPMU_PLL1REG1 >> 23) & 0xF;

         core_clk_div = APMU_CCR & 3;

         ddr_clk_div = (APMU_CCR >> 12) & 3;

         core_clk = pll1_out / (core_clk_div + 1);
         if (vco_div_sel_se == 1)
            core_clk = core_clk * 2 / 3;
         else if (vco_div_sel_se == 2)
            core_clk >>= 1;

         ddr_clk = pll1_out / (2 * (ddr_clk_div+1));

         if (vco_div_sel_se == 1)
            ddr_clk = ddr_clk * 2 / 3;
         else if (vco_div_sel_se == 2)
               ddr_clk >>= 1;

         *pclk = core_clk / 1000000;
         *dclk = ddr_clk / 1000000;
      }
      break;
   }

   MPMU_FCCR = mpmu_fccr;     // restore it
}
