/* Copyright 2008-2011, Unpublished Work of Technologic Systems
 * All Rights Reserved.
 *
 * THIS WORK IS AN UNPUBLISHED WORK AND CONTAINS CONFIDENTIAL,
 * PROPRIETARY AND TRADE SECRET INFORMATION OF TECHNOLOGIC SYSTEMS.
 * ACCESS TO THIS WORK IS RESTRICTED TO (I) TECHNOLOGIC SYSTEMS 
 * EMPLOYEES WHO HAVE A NEED TO KNOW TO PERFORM TASKS WITHIN THE SCOPE
 * OF THEIR ASSIGNMENTS AND (II) ENTITIES OTHER THAN TECHNOLOGIC
 * SYSTEMS WHO HAVE ENTERED INTO APPROPRIATE LICENSE AGREEMENTS.  NO
 * PART OF THIS WORK MAY BE USED, PRACTICED, PERFORMED, COPIED, 
 * DISTRIBUTED, REVISED, MODIFIED, TRANSLATED, ABRIDGED, CONDENSED, 
 * EXPANDED, COLLECTED, COMPILED, LINKED, RECAST, TRANSFORMED, ADAPTED
 * IN ANY FORM OR BY ANY MEANS, MANUAL, MECHANICAL, CHEMICAL, 
 * ELECTRICAL, ELECTRONIC, OPTICAL, BIOLOGICAL, OR OTHERWISE WITHOUT
 * THE PRIOR WRITTEN PERMISSION AND CONSENT OF TECHNOLOGIC SYSTEMS.
 * ANY USE OR EXPLOITATION OF THIS WORK WITHOUT THE PRIOR WRITTEN
 * CONSENT OF TECHNOLOGIC SYSTEMS COULD SUBJECT THE PERPETRATOR TO
 * CRIMINAL AND CIVIL LIABILITY.
 */

/* To compile xuartctl, use the appropriate cross compiler and run the
 * command:
 *
 *   gcc -fno-tree-cselim -Wall -O -mcpu=arm9 -o xuartctl xuartctl.c -lutil
 *
 * xuartcore.c should be in the same directory as xuartctl.c
 *
 * On uclibc based initrd's, the following additional gcc options are
 * necessary: -Wl,--rpath,/slib -Wl,-dynamic-linker,/slib/ld-uClibc.so.0
 */

const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ ;

#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sched.h>
#include <getopt.h>
#if defined(__NetBSD__) || defined (__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <termios.h>
#include <ctype.h>
#include <limits.h>
#ifdef __linux__
#include <linux/serial.h>
#endif

#define __swp(val, adr)				\
	asm volatile (				\
		"swp %[v], %[v], [%[a]]\n"	\
		: [v] "+r"(val)			\
		: [a] "r"(adr)			\
	)

#ifndef TEMP_FAILURE_RETRY
# define TEMP_FAILURE_RETRY(expression) \
  (__extension__                                                              \
    ({ long int __result;                                                     \
       do __result = (long int) (expression);                                 \
       while (__result == -1L && errno == EINTR);                             \
       __result; }))
#endif

static int do_proxy(char *);
static int do_client(void);
static int do_test(int);

static int terminate = 0;
static int irqfd = 0;
static void termsig(int x) {
	terminate = 1;
	if (irqfd >= 0) close(irqfd); /* Hopefully wakes up the select */
}

static void xu_signal(void *x, int sig) {
	int *i = (int *)x;
	*i |= (1 << sig);
}

int alarmed = 0;
static void alarmsig(int x) {
	alarmed = 1;
}

int hup = 0;
static void hupsig(int x) {
	hup = 1;
}

static void reservemem(void) {
#ifdef __linux__
	char dummy[32768];
	int i, pgsize;
	FILE *maps;

	pgsize = getpagesize();
	mlockall(MCL_CURRENT|MCL_FUTURE);
	for (i = 0; i < sizeof(dummy); i += 4096) {
		dummy[i] = 0;
	}

	maps = fopen("/proc/self/maps", "r"); 
	if (maps == NULL) {
		perror("/proc/self/maps");
		exit(1);
	}
	while (!feof(maps)) {
		size_t s, e, x;
		char perms[16];
		int r = fscanf(maps, "%zx-%zx %s %*x %zx:%*x %*d",
		  &s, &e, perms, &x);
		if (r == EOF) break;
		assert (r == 4);

		i = 0;
		while ((r = fgetc(maps)) != '\n') {
			if (r == EOF) break;
		}
		assert(s <= e && (s & 0xfff) == 0);
		if (perms[0] == 'r' && perms[3] == 'p' && x != 1)
		  while (s < e) {
			volatile unsigned char *ptr = (unsigned char *)s;
			unsigned char d;
			d = *ptr;
			if (perms[1] == 'w') *ptr = d;
			s += pgsize;
		}
	}
#endif
}

static int semid = -1;
static int sbuslocked = 0;
static volatile unsigned int *cvspiregs = NULL;
static volatile unsigned int *cvgpioregs = NULL;
static void sbuslock(void) {
	int r;
	struct sembuf sop;
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
	r = TEMP_FAILURE_RETRY(semop(semid, &sop, 1));
	assert (r == 0);
	sbuslocked = 1;
	cvgpioregs[0] = (1<<15|1<<17|1<<3);
}

static void sbusunlock(void) {
	struct sembuf sop = { 0, 1, SEM_UNDO};
	int r;
	if (!sbuslocked) return;
	r = semop(semid, &sop, 1);
	assert (r == 0);
	sbuslocked = 0;
}

static void sbusyield(void) {
	sbusunlock();
	usleep(9999);
	sbuslock();
}

static int cvwait(void *arg, int c) {
	return -1;
}


static int nbuslocked = 0;
static volatile unsigned int *mxgpioregs = NULL;
void nbuslock(void)
{
	int r;
	struct sembuf sop;

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


	/*Wrapper added to retry in case of EINTR*/
	r = TEMP_FAILURE_RETRY(semop(semid, &sop, 1));
	assert(r == 0);

	nbuslocked = 1;
}

void nbusunlock(void) 
{
	struct sembuf sop = { 0, 1, SEM_UNDO};
	int r;
	if(nbuslocked) {
		r = semop(semid, &sop, 1);
		assert(r == 0);
		nbuslocked = 0;
	}
}

void nbusyield(void)
{
	int r;
	r = semctl(semid, 0, GETNCNT);
	assert(r != -1);
	if(r) {
		nbusunlock();
		usleep(9999);
		nbuslock();
	}
}

struct xuartcore;

#ifndef PPC
static void cvpoke16(unsigned int adr, unsigned short dat) {
#if defined(__arm__) && !defined(__thumb__)
	unsigned int dummy = 0;
	unsigned int d = dat;

	asm volatile (
		"mov %0, %1, lsl #18\n"
		"orr %0, %0, #0x800000\n"
		"orr %0, %0, %2, lsl #3\n"
		"3: ldr r1, [%3, #0x64]\n"
		"cmp r1, #0x0\n"
		"bne 3b\n"
		"2: str %0, [%3, #0x50]\n"
		"1: ldr r1, [%3, #0x64]\n"
		"cmp r1, #0x0\n"
		"beq 1b\n"
		"ldr %0, [%3, #0x58]\n"
		"ands r1, %0, #0x1\n"
		"moveq %0, #0x0\n"
		"beq 3b\n"
		: "+r"(dummy) : "r"(adr), "r"(d), "r"(cvspiregs) : "r1","cc"
	);
#endif
}

static unsigned short cvpeek16(unsigned int adr) {
#if defined(__arm__) && !defined(__thumb__)
	unsigned short ret = 0;

	asm volatile (
		"mov %0, %1, lsl #18\n"
		"2: str %0, [%2, #0x50]\n"
		"1: ldr r1, [%2, #0x64]\n"
		"cmp r1, #0x0\n"
		"beq 1b\n"
		"ldr %0, [%2, #0x58]\n"
		"ands r1, %0, #0x10000\n"
		"bicne %0, %0, #0xff0000\n"
		"moveq %0, #0x0\n"
		"beq 2b\n" 
		: "+r"(ret) : "r"(adr), "r"(cvspiregs) : "r1", "cc"
	);

	return ret;
#else
	return 0;
#endif
}

void mxpoke16(unsigned char adr, unsigned short dat)
{
	int x;
	
	mxgpioregs[0x708/4] = (1 << 16) | (1 << 26) | (1 << 25) |
	  (1 << 27) | (0xff);
	mxgpioregs[0x704/4] = ((1 << 26) | adr );
	mxgpioregs[0x704/4] = (1 << 25);

	for(x = 1; x >= 0; x--) {
        	mxgpioregs[0x708/4] = (1 << 16) | (1 << 26) | (1 << 25) |
		  (1 << 27) | (0xff);
		mxgpioregs[0x704/4] = (unsigned char)(dat >> (x*8));
		mxgpioregs[0x704/4] = (1 << 25);
	}
	mxgpioregs[0x704/4] = (1 << 16) | (1 << 27);

	while(mxgpioregs[0x900/4] & (1 << 21)) {
		mxgpioregs[0x708/4] = (1 << 16) | (1 << 26) | (1 << 25) |
		  (1 << 27) | (0xff);
		mxgpioregs[0x704/4] = (1 << 16) | (1 << 27);
	}
}

unsigned short mxpeek16(unsigned char adr)
{
	int x;
	unsigned short ret;

	mxgpioregs[0x708/4] = (1 << 16) | (1 << 25) | (1 << 24) |
	  (1 << 27) | (0xff);
	mxgpioregs[0x704/4] = (1 << 26) | adr;
	mxgpioregs[0x704/4] = (1 << 25);
	mxgpioregs[0xb00/4] = 0xffdfff00;

	do {
		ret = 0;
		for(x = 1; x >= 0; x--) {
			mxgpioregs[0x708/4] = (1 << 16) | (1 << 25) |
			  (1 << 24) | (1 << 27) | (0xff);
			mxgpioregs[0x704/4] = (1 << 25);
			ret |= ((mxgpioregs[0x900/4] & 0xff) << (x*8));
		}
		mxgpioregs[0x704/4] = (1 << 16) | (1 << 24) | (1 << 27);
	} while(mxgpioregs[0x900/4] & (1 << 21)); 

	mxgpioregs[0xb00/4] = 0xffdfffff;

	return ret;
}

static void xupoke16(struct xuartcore *, unsigned int, unsigned short);
static void 
xxupoke16(struct xuartcore *xu, unsigned int adr, unsigned short val)  {
	if (!cvspiregs && !mxgpioregs) xupoke16(xu, adr, val);
	else if(mxgpioregs) mxpoke16(adr+0x40, val);
	else cvpoke16(adr, val);
}

static unsigned short xupeek16(struct xuartcore *, unsigned int);
static unsigned short
xxupeek16(struct xuartcore *xu, unsigned int adr) {
	if (!cvspiregs && !mxgpioregs) return xupeek16(xu, adr);
	else if(mxgpioregs) return mxpeek16(adr+0x40);
	else return cvpeek16(adr);
}

static size_t memwindow_adr = 0;
static void poke16(volatile unsigned short *adr, unsigned short val);
static void xpoke16(volatile unsigned short *adr, unsigned short val) {
	if (!cvspiregs && !mxgpioregs) poke16(adr, val);
	else if(mxgpioregs) {
		if(memwindow_adr != (size_t)adr) 
		  mxpoke16(0x5c, (size_t)adr);
		memwindow_adr = (size_t)adr + 2;
		mxpoke16(0x5e, val);
	} else {
		if (memwindow_adr != (size_t)adr) {
			cvpoke16(0x1c, (size_t)adr);
		}
		memwindow_adr = (size_t)adr + 2;
		cvpoke16(0x1e, val);
	}
}

static unsigned short peek16(volatile unsigned short *);
static unsigned short xpeek16(volatile unsigned short *adr) {
	if (!cvspiregs && !mxgpioregs) return peek16(adr);
	else if(mxgpioregs) {
		if(memwindow_adr != (size_t)adr) 
		  mxpoke16(0x5c, (size_t)adr);
		memwindow_adr = (size_t)adr + 2;
		return mxpeek16(0x5e);
	} else {
		if (memwindow_adr != (size_t)adr) {
			cvpoke16(0x1c, (size_t)adr);
		}
		memwindow_adr = (size_t)adr + 2;
		return cvpeek16(0x1e);
	}
}

#endif
#ifndef PPC
#define XUPOKE16(x, y, z) xxupoke16((x), (y), (z))
#define XUPEEK16(x, y) xxupeek16((x), (y))
#define PEEK16(x) xpeek16((x))
#define POKE16(x, y) xpoke16((x), (y))
#endif
#include "xuartcore.c"


static unsigned int is_cavium(void) __attribute__((pure));
static unsigned int is_cavium(void) {
#ifdef __linux__
	FILE *cpuinfo;
	char buf[4096];
	static unsigned int checked = 0;
	static unsigned int iscavium = 0;

	if (!checked) {
		cpuinfo = fopen("/proc/cpuinfo", "r");
		if (cpuinfo == NULL) {
			perror("/proc/cpuinfo");
			exit(4);
		}
		bzero(buf, 4096);
		fread(buf, 1, 4095, cpuinfo);
		fclose(cpuinfo);
		if (strstr(buf, "FA526")) iscavium = 1;
		checked = 1;
	}
	return iscavium;
#else
	return 0;
#endif
}

static unsigned int is_mx28(void) __attribute__((pure));
static unsigned int is_mx28(void) {
	FILE *cpuinfo;
	char buf[4096];
	static unsigned int checked = 0;
	static unsigned int ismx28 = 0;

	if(!checked) {
		cpuinfo = fopen("/proc/cpuinfo", "r");
		if(cpuinfo == NULL) {
			perror("/proc/cpuinfo");
			exit(4);
		}
		bzero(buf, 4096);
		fread(buf, 1, 4095, cpuinfo);
		fclose(cpuinfo);
		if(strstr(buf, "TS-7600/TS-4600")) ismx28 = 1;
		checked = 1;
	}
	return ismx28;
}

static unsigned int is_mohawk(void) __attribute__((pure)); 
static unsigned int is_mohawk(void) {
	FILE *cpuinfo;
	char buf[4096];
	static unsigned int checked = 0;
	static unsigned int ismohawk = 0;

	if (!checked) {
		cpuinfo = fopen("/proc/cpuinfo", "r");
		if (cpuinfo == NULL) {
			perror("/proc/cpuinfo");
			exit(4);
		}
		bzero(buf, 4096);
		fread(buf, 1, 4095, cpuinfo);
		fclose(cpuinfo);
		if (strstr(buf, "Mohawk")) ismohawk = 1;
		checked = 1;
	}
	return ismohawk;
}

static unsigned int is_armv7(void) __attribute__((pure)); 
static unsigned int is_armv7(void) {
	FILE *cpuinfo;
	char buf[4096];
	static unsigned int checked = 0;
	static unsigned int isarmv7 = 0;

	if (!checked) {
		cpuinfo = fopen("/proc/cpuinfo", "r");
		if (cpuinfo == NULL) {
			perror("/proc/cpuinfo");
			exit(4);
		}
		bzero(buf, 4096);
		fread(buf, 1, 4095, cpuinfo);
		fclose(cpuinfo);
		if (strstr(buf, "ARMv7")) isarmv7 = 1;
		checked = 1;
	}
	return isarmv7;
}


static unsigned char rawmode = 0;
static unsigned char hasoddbyte = 0;
static int 
xxu_writec(struct xuartcore *xu, int uartn, unsigned char *buf, int len) {
	static char oddbytes[8];
	int r = 0;
	int n;
	unsigned short *s;
	int slen;
	unsigned short sbuf[2048];

	if (hup) return r;

	if (!(rawmode & (1 << uartn))) return xu_writec(xu, uartn, buf, len);

	n = 0;

	/* XXX: assumes little-endian host */
	if ((hasoddbyte & (1 << uartn)) && len) {
		unsigned short x;
		x = oddbytes[uartn] | *buf << 8;
		r = xu_write(xu, uartn, &x, 1);
		if (r == 1) {
			buf++;
			n++;
			len--;
			hasoddbyte &= ~(1 << uartn);
		} else return 0;
	}

	if (len > 1) {
		int i;
		slen = len / 2;
		if ((size_t)buf & 0x1) {
			/* relocate to 16-bit aligned buf */
			assert(0);
			for (i = 0; i < slen; i++) {
				sbuf[i] = buf[i*2] | (buf[i*2+1] << 8);
			}
			s = sbuf;
		} else s = (unsigned short *)buf;
		r = xu_write(xu, uartn, s, slen);
		n += r * 2;
		len -= r * 2;
		buf += r * 2;
	}

	if (len == 1) {
		oddbytes[uartn] = *buf;
		hasoddbyte |= (1 << uartn);
		len--;
		n++;
	}

	return n;
}

static int
xxu_readc(struct xuartcore *xu, int uartn, unsigned char *buf, int len) {
	int r = 0;

	if (hup) return r;

	if (!(rawmode & (1 << uartn))) return xu_readc(xu, uartn, buf, len);

	assert (((size_t)buf & 0x1) == 0);
	assert (len < 0 || (len & 0x1) == 0);
	if (len > 0) len = len >> 1;
	r = xu_read(xu, uartn, (unsigned short *)buf, len);
	if (r > 0) r *= 2;
	return r;
}

static
int server_running(void) {
	struct sockaddr_in sa;
	int sk, r;

	/* Currently, we just check if we can bind to port 7350 */
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = INADDR_ANY;
	sa.sin_port = htons(7350);
	sk = socket(PF_INET, SOCK_STREAM, 0);
	assert(sk != -1);
	r = bind(sk, (struct sockaddr *)&sa, sizeof(sa));
	close(sk);
	return (r != 0);
}

char *opt_mode = "8n1";
int opt_mode_set = 0;
int opt_baudrate_set = 0;
char *opt_port = NULL;
int opt_ptyargc = 0;
int opt_test = 0;
char **opt_ptyargv = NULL;
int opt_irq = 32, opt_baudrate = 115200;
unsigned int opt_regstart = 0x600ff100;
unsigned int opt_memstart = 0x60000000;
unsigned int opt_server = 0;
struct in_addr opt_bind = { INADDR_ANY };

void usage(char **argv) {
	fprintf(stderr, 
	  "Usage: %s [OPTION] ...\n"
	  "       %s --port=PORT [OPTION] ... -- [COMMAND] [ARGS]\n"
	  "Technologic Systems XUART core userspace driver utility.\n"
	  "Example: %s --server\n"
	  "         %s --port=192.168.0.50:7350 --speed=9600 -- /bin/sh -i\n"
	  "         %s --port=0 --test\n"
	  "\n"
	  "  -i, --irq=N             Use IRQ N as XUART IRQ (32)\n"
	  "  -r, --regstart=ADD      Use ADD address as regstart (0x600ff100)\n"
	  "  -m, --memstart=ADD      Use ADD address as memstart (0x60000000)\n"
	  "  -s, --speed=BAUD        Use BAUD as default baudrate (115200)\n"
	  "  -o, --mode=MODE         Use MODE as default mode (8n1)\n"
	  "  -d, --server            Daemonize and run as server\n"
	  "  -I, --bind=IPADDR       Bind server to IPADDR\n"
	  "  -p, --port=PORT         Connect to local or remote XUART port\n"
	  "  -t, --test              Run loopback and latency test\n"
	  "  -P, --proxy[=DEV]       Use tty device DEV instead of raw XUART HW\n"
	  "  -h, --help              This help\n"
	  "\n"
	  "When run as a server, default is to listen at TCP port numbers starting at\n"
	  "7350 with 1 port per XUART channel.\n"
	  "\n"
	  "PORT option can be a either a number 0-7, or a HOSTNAME:TCPPORT for a\n"
	  "remote TCP socket.  When both --port and --server are used, a pseudo-tty\n"
	  "is allocated and connected to the XUART channel and pseudo-tty processing\n"
	  "continues in the background.  When only --port is specified and no command\n"
	  "is given, stdin and stdout are connected to the XUART channel, otherwise\n"
	  "COMMAND is run as a sub-program with its stdin/stdout/stderr connected to\n"
	  "the allocated pseudo-tty.\n",
	  argv[0], argv[0], argv[0], argv[0], argv[0]
	);
}

int main(int argc, char **argv) {
	struct xuartcore xu;
	int infd[8], remfd[8];
	int c, devmem, xu_pending, localport = 0;
	char fbuf[32];
	// struct sched_param sched;
	struct sockaddr_in sa;
	char * opt_proxy = NULL;
	int q, urgent, i, nchan, maxfd = 0, opt_proxy_set = 0;
	fd_set rfds, wfds;
	struct timeval tv;
	unsigned short wbufremain[8], wbufidx[8];
	unsigned short rbufremain[8], rbufidx[8];
	unsigned char wbuf[8][2048];
	unsigned char rbuf[8][2048];
	char cmdbuf[8][32];
	char modes[8][32];
	int bauds[8], rlw[8];
	int autoterminate, rerx = 0, overflowing = 0, irqpoll = 0;
	key_t shmkey;
	int shmid;
	volatile unsigned long long *sbus_shm;
	static struct option long_options[] = {
	  { "bind", 1, 0, 'I' },
	  { "irq", 1, 0, 'i' },
	  { "regstart", 1, 0, 'r' },
	  { "memstart", 1, 0, 'm' },
	  { "speed", 1, 0, 's' },
	  { "mode", 1, 0, 'o' },
	  { "port", 1, 0, 'p' },
	  { "server", 0, 0, 'd' },
	  { "test", 0, 0, 't' },
	  { "proxy", 2, 0, 'P' },
	  { "help", 0, 0, 'h' },
	  { 0, 0, 0, 0 }
	};

	for (i = 0; i < 3; i++) if (fcntl(i, F_GETFD) == -1) 
	  dup2(i ? i - 1 : open("/dev/null", O_RDWR), i);

	if (is_cavium()) reservemem();
	else if (is_mohawk()) {
		opt_irq = 64;
		opt_regstart = 0x80005400;         
		opt_memstart = 0x80000000;            
	} else if (is_armv7()) { /* A TS-4800 ... */
		opt_irq = 272;
		opt_regstart = 0xb0018000;         
		opt_memstart = 0xb0000000;            
	}

	while((c = getopt_long(argc, argv, "hi:r:m:o:s:dp:tI:P::",
	  long_options, NULL)) != -1) {
		switch (c) {
		case 'P':
			opt_proxy_set = 1;
			if (optarg) opt_proxy = strdup(optarg);
			break;
		case 'I':
			i = inet_aton(optarg, &opt_bind);
			if (i == 0) {
				fprintf(stderr, "Bad arg: %s\n", optarg);
				return 3;
			}
			break;
		case 't':
			opt_test = 1;
			break;
		case 'p':
			localport = 0;
			if (strlen(optarg) == 1 && isdigit(*optarg)) {
				char nbuf[32];
				snprintf(nbuf, sizeof(nbuf), "127.0.0.1:%d", 
		  		  7350 + *optarg - '0');
				opt_port = strdup(nbuf);
				localport = 1;
			} else opt_port = strdup(optarg);
			break;
		case 'd':
			opt_server = 1;
			break;
		case 'i':
			if (strstr(optarg, "hz")) irqpoll = 1;
			if (strstr(optarg, "Hz")) irqpoll = 1;
			if (strstr(optarg, "HZ")) irqpoll = 1;
			if (strstr(optarg, "hZ")) irqpoll = 1;
			opt_irq = strtoul(optarg, NULL, 0);
			break;
		case 'r':
			opt_regstart = strtoul(optarg, NULL, 0);
			break;
		case 'm':
			opt_memstart = strtoul(optarg, NULL, 0);
			break;
		case 'o':
			opt_mode = strdup(optarg);
			opt_mode_set = 1;
			break;
		case 's':
			opt_baudrate = strtoul(optarg, NULL, 0);
			opt_baudrate_set = 1;
			break;
		default:
			usage(argv);
			return 1;
		}
	}

	if (opt_test && !opt_port) {
		fprintf(stderr, "%s: --test option requires --port\n", argv[0]);
		return 1;
	}

	if (opt_proxy_set && !opt_server) {
		fprintf(stderr, "%s: --proxy option requires --server\n", 
		  argv[0]);
		return 1;
	}

	if (opt_test) { /* Test requires 115200@8n1 */
		opt_baudrate = 115200;
		opt_baudrate_set = 1;
		opt_mode = "8n1";
		opt_mode_set = 1;
	}

	opt_ptyargc = argc - optind;
	opt_ptyargv = argv + optind;

	if ((opt_port && !localport) || 
	  (opt_port && localport && server_running())) return do_client();

	if (opt_proxy_set && opt_server) return do_proxy(opt_proxy);

	if (!opt_server && opt_port && localport) opt_server = 1;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	if (!irqpoll) {
		snprintf(fbuf, sizeof(fbuf), "/proc/irq/%d/irq", opt_irq);
		irqfd = open(fbuf, O_RDONLY|O_SYNC|O_NONBLOCK);
		if (irqfd == -1) opt_irq = 9999; /* Default 100hz */
	} else {
		opt_irq = (1000000 / opt_irq) - 1;
		irqfd = -1;
	}
	devmem = open("/dev/mem", O_RDWR|O_SYNC);
	if (devmem == -1) {
		perror("/dev/mem");
		return 3;
	}
	xu.xu_osarg = &xu_pending;
	xu.xu_wait = NULL;
	xu.xu_signal = xu_signal;
	xu.xu_maskirq = NULL;
	xu.xu_atomic_set = NULL;
	xu.xu_rxbreak = NULL;
	xu.xu_rxidle = NULL;

	shmkey = 0x75000000;
	shmid = shmget(shmkey, 0x1000, IPC_CREAT|0666);
	assert(shmid != -1);
	sbus_shm = shmat(shmid, NULL, 0);
	sbus_shm += 48;
	if (getpriority(PRIO_PROCESS, sbus_shm[0]) == -1 &&
	  errno == ESRCH) sbus_shm[0] = 0;
	if (sbus_shm[0]) {
		fprintf(stderr, "xuarts_detected=%lld\n", sbus_shm[1]);
		fprintf(stderr, "xuartctl_server_pid=%lld\n", sbus_shm[0]);
		fprintf(stderr, "xuartctl_wakeups=%lld\n", sbus_shm[2]);
		fprintf(stderr, "xuartctl_txbytes=%lld\n", sbus_shm[3]);
		fprintf(stderr, "xuartctl_rxbytes=%lld\n", sbus_shm[4]);
		return 0;
	}

#ifndef PPC
	if (is_cavium()) {
		cvspiregs = (unsigned int *) mmap(0, 4096,
		  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x71000000);
		cvgpioregs = (unsigned int *) mmap(0, 4096,
		  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x7c000000);

		sbuslock();
		cvgpioregs[0] = (1<<15|1<<17|1<<3);
		cvspiregs[0x64 / 4] = 0x0; /* RX iRQ threahold 0 */
		cvspiregs[0x40 / 4] = 0x80000c02; /* 24-bit mode, no byte swap */
		cvspiregs[0x60 / 4] = 0x0; /* 0 clock inter-transfer delay */
		cvspiregs[0x6c / 4] = 0x0; /* disable interrupts */
		cvspiregs[0x4c / 4] = 0x4; /* deassert CS# */
		for (i = 0; i < 8; i++) cvspiregs[0x58 / 4];
		xu.xu_regstart = xu.xu_memstart = 0;
		xu.xu_wait = cvwait;
		cvpoke16(0x1c, 0); /* initialize memwindow_adr */
	} else if(is_mx28()) {
		mxgpioregs = (unsigned int *) mmap(0, 4096,
		  PROT_READ|PROT_WRITE, MAP_SHARED, devmem, 0x80018000);

		nbuslock();
		mxgpioregs[0x100/4] = 0xffffffff; //Set pinmux to GPIO
		mxgpioregs[0x110/4] = 0xffffffff; //Set pinmux to GPIO
		mxgpioregs[0x700/4] = 0xffffffff; //Set all to logic high
		mxgpioregs[0xb00/4] = 0xffdfffff; //Set all to output
		xu.xu_regstart = xu.xu_memstart = 0;
		xu.xu_wait = cvwait;
	} else
		 
#endif
	{
		xu.xu_regstart = (size_t) mmap(0, 4096,
		  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 
		  opt_regstart & ~0xfff);
		xu.xu_regstart += opt_regstart & 0xfff;	
		xu.xu_memstart = (size_t) mmap(0, 8192,
		  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 
		  opt_memstart & ~0xfff);
		xu.xu_memstart += opt_memstart & 0xfff;
	}

	nchan = xu_reset(&xu);
	if (is_cavium()) sbusunlock();
	if (is_mx28()) nbusunlock();
	if (!opt_server) {
		if (nchan > 8) nchan = 0;
		fprintf(stderr, "xuarts_detected=%d\n", nchan);
		fprintf(stderr, "xuartctl_server_pid=0\n");
		fprintf(stderr, "xuartctl_wakeups=0\n");
		fprintf(stderr, "xuartctl_txbytes=0\n");
		fprintf(stderr, "xuartctl_rxbytes=0\n");
		return 0;
	}

	assert(nchan > 0 && nchan <= 8);

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = opt_bind.s_addr;
	for (i = 0; i < nchan; i++) {
		int r, sk, x = 1;
		/* TCP server socket */
		sa.sin_port = htons(7350 + i);
		sk = socket(PF_INET, SOCK_STREAM, 0);
		setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, &x, 4);
		assert(sk != -1);
		r = bind(sk, (struct sockaddr *)&sa, sizeof(sa));
		if (r) {
			perror("bind");
			return 1;
		}
		r = listen(sk, 1);
		assert(r != -1);
		r = fcntl(sk, F_SETFL, O_NONBLOCK);
		assert(r != -1);
		if (sk > maxfd) maxfd = sk;
		FD_SET(sk, &rfds);
		infd[i] = sk;

		remfd[i] = -1;
		wbufremain[i] = rbufremain[i] = 0;
		strncpy(modes[i], opt_mode, 32);
		bauds[i] = opt_baudrate;
		rlw[i] = 1;
		cmdbuf[i][0] = 0;
	}
	if (strstr(opt_mode, "raw")) rawmode = 0xff;
	for (i = 0; i < nchan; i++) {
		char *xptr;
		if ((xptr = strstr(modes[i], ",rlw="))) {
			char *yptr = xptr;
			xptr += 5;
			rlw[i] = strtoul(xptr, &xptr, 0);
			if (rlw[i] > 256) rlw[i] = 256;
			else if (rlw[i] == 0) rlw[i] = 1;
			do {*yptr++ = *xptr++;} while (*xptr);
		}
	}

	if (opt_port && localport) {
		if (fork() != 0) return do_client();
		autoterminate = 1;
	} else autoterminate = 0;

	daemon(0, 0);
//	sched.sched_priority = 25;
//	sched_setscheduler(0, SCHED_FIFO, &sched);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, hupsig);
	signal(SIGCHLD, SIG_IGN);
	signal(SIGTERM, termsig);
	signal(SIGINT, termsig);

	sbus_shm = shmat(shmid, NULL, 0);
	sbus_shm += 48;
	sbus_shm[0] = getpid();
	sbus_shm[1] = nchan;

	urgent = 0;

superloop_start:
	if (is_cavium()) sbusunlock();
	if (is_mx28()) nbusunlock();
	if (irqfd >= 0) {
		int n;
		FD_SET(irqfd, &rfds);
		n = select(maxfd + 1, &rfds, &wfds, NULL, NULL);
		if (is_cavium()) sbuslock();
		if (is_mx28()) nbuslock();
		if (n == -1 && errno == EINTR) {
			FD_ZERO(&rfds);
			FD_ZERO(&wfds);
		}
		if (hup) q = 0;
		else if (FD_ISSET(irqfd, &rfds)) q = xu_irq(&xu);
		else q = xu.status;
	} else {
		int n;
		tv.tv_sec = 0;
		tv.tv_usec = opt_irq;
		n = select(maxfd + 1, &rfds, &wfds, NULL, &tv);
		if (is_cavium()) sbuslock();
		if (is_mx28()) nbuslock();
		if (n == -1 && errno == EINTR) {
			FD_ZERO(&rfds);
			FD_ZERO(&wfds);
		}
		if (hup) q = 0; else q = xu_irq(&xu);
	}
	sbus_shm[2]++; /* xuartctl wakeups */
		
service_channels:
	for (i = 0; i < nchan; i++, q >>= 1) {
		int r;
		if (remfd[i] == -1) continue;

		if (!wbufremain[i] && FD_ISSET(remfd[i], &rfds) && !rerx) { 
			/* new data from net? */
			int w, n;
			if (urgent & (1 << i)) {
				/* Check OOB */
				r = ioctl(remfd[i], SIOCATMARK, &n);
				if (r == -1) goto eof;
				if (!n) urgent &= ~(1 << i);
				else urgent |= (1 << (i + 8));
			}
			n = read(remfd[i], wbuf[i], 2048);
			if (n == -1 && (errno != EAGAIN && errno != EINTR)) {
				/* temp failure, do nothing */
			} else if (n <= 0) {
				/* socket EOF or unexpected error */
				goto eof;
			} else if (urgent & (1 << (i + 8))) {
				/* command mode socket */
				if ((wbufidx[i] + n) > 32) n = 0;
				memcpy(&cmdbuf[i][wbufidx[i]], wbuf[i], n);
				wbufidx[i] += n;
			} else if ((w = xxu_writec(&xu, i, wbuf[i], n)) != n) {
				/* xuart is full, have to finish later */
				wbufremain[i] = n - w;
				wbufidx[i] = w;
				sbus_shm[3] += w; /* TX bytes */
				FD_CLR(remfd[i], &rfds); /* stop reading */
			} else sbus_shm[3] += w;
		} else if (wbufremain[i] && (q & 0x100)) {
			/* old data remaining from net? */
			int w;
			int n = wbufremain[i];
			w = xxu_writec(&xu, i, &wbuf[i][wbufidx[i]], n);
			if (w == n) FD_SET(remfd[i], &rfds);
			else FD_CLR(remfd[i], &rfds); /* stop reading */
			wbufidx[i] += w;
			wbufremain[i] -= w;
		} else if (!wbufremain[i]) FD_SET(remfd[i], &rfds); 

		if (q & 0x1) {
			r = xxu_readc(&xu, i, NULL, -rlw[i]);
			if (r == 0) xu.status &= ~(1<<i);
		} else r = 0;
		assert(r != -1);

		if (r && !rbufremain[i]) {
			/* new data from xuart? */
			int w;
			r = xxu_readc(&xu, i, rbuf[i], r);
			assert(r != -1);
			sbus_shm[4] += r; /* RX bytes */
			if (r != 0) w = write(remfd[i], rbuf[i], r);
			else w = 0;
			if (w == -1 && (errno != EAGAIN && errno != EINTR)) {
				/* XXX: rethink socket shutdown */
				goto eof;
			} else if (w == -1) {
				/* socket is busy, have to finish later */
				rbufremain[i] = r;
				rbufidx[i] = 0;
				FD_SET(remfd[i], &wfds);
			} else if (r != w) {
				/* socket is full, have to finish rest later */
				rbufremain[i] = r - w;
				rbufidx[i] = w; 
				/* max RX IRQ pushback */
				xxu_readc(&xu, i, NULL, 0);
				FD_SET(remfd[i], &wfds);
			} 
		} else if (rbufremain[i] && FD_ISSET(remfd[i], &wfds) && !rerx) {
			/* old data remaining from xuart? */
			int w;
			int n = rbufremain[i];
			w = write(remfd[i], &rbuf[i][rbufidx[i]], n);
			if (w == -1 && (errno != EAGAIN && errno != EINTR)) {
				goto eof;
			} else if (w == -1) {
				/* temporary failure */
			} else if (w == n) {
				FD_CLR(remfd[i], &wfds);
				rbufremain[i] = 0;
				overflowing &= ~(1 << i);
			} else { /* w != n */
				rbufidx[i] += w;
				rbufremain[i] -= w;
			}
		} else if (rbufremain[i]) FD_SET(remfd[i], &wfds);

		if (r && rbufremain[i]) overflowing |= (1 << i);

		/* Shut down a existing socket if a new connect is pending */
		if (!FD_ISSET(infd[i], &rfds)) continue;

eof:
		fcntl(remfd[i], F_SETFL, 0);
		if (is_cavium()) sbusunlock();
		if (is_mx28()) nbusunlock();
		TEMP_FAILURE_RETRY(close(remfd[i]));
		if (is_cavium()) sbuslock();
		if (is_mx28()) nbuslock();
		if (!hup) {
			int t;
			if (is_cavium()) xu.xu_wait = cvwait;
			if (is_mx28()) xu.xu_wait = cvwait;
			do { 
				t = xu_close(&xu, i); 
				if (t && is_cavium()) sbusyield();
				if (t && is_mx28()) nbusyield();
			} while (t != 0 && !hup);
			if (is_cavium()) xu.xu_wait = NULL;
			if (is_mx28()) xu.xu_wait = NULL;
		}
		FD_CLR(remfd[i], &rfds);
		FD_CLR(remfd[i], &wfds);
		remfd[i] = -1;
		overflowing |= (1<<i);
		/* commit port changes */
		if ((urgent & (1 << (i + 8))) && cmdbuf[i]) { 
			char *xptr;
			int t;
			/* command */
			if ((xptr = strstr(cmdbuf[i], ",rlw="))) {
				char *yptr = xptr;
				xptr += 5;
				rlw[i] = strtoul(xptr, &xptr, 0);
				if (rlw[i] > 256) rlw[i] = 256;
				else if (rlw[i] == 0) rlw[i] = 1;
				do {*yptr++ = *xptr++;} while (*xptr);
			}
			t = strtoul(cmdbuf[i], &xptr, 0);
			if (t != 0) bauds[i] = t;
			if (*xptr == '@') {
				xptr++;
				strncpy(modes[i], xptr, 32);
			}
			if (strstr(modes[i], "raw")) rawmode |= (1 << i);
			else rawmode &= ~(1 << i);
			cmdbuf[i][0] = 0;

		} else if (autoterminate) {
			int nopen = 0;
			for (i = 0; i < nchan; i++) if (remfd[i] != -1) nopen++;
			if (nopen == 0) return 0;
		}
	}

	if (xu.status & 0xff & ~overflowing) {
		rerx = 1;
		q = xu.status;
		goto service_channels;
	} else if (irqfd >= 0 && FD_ISSET(irqfd, &rfds)) {
		//int t;
		TEMP_FAILURE_RETRY(close(irqfd));
		//sched_yield();
		irqfd = open(fbuf, O_RDONLY|O_SYNC|O_NONBLOCK);
		//read(irqfd, &t, sizeof(t));
	}
	rerx = 0;

	for (i = 0; i < nchan; i++) {
		/* new connection requests pending accept? */
		if (FD_ISSET(infd[i], &rfds)) {
			int r, sk, tos = IPTOS_LOWDELAY;
			int x;
			struct linger l;
			assert(remfd[i] == -1);
			sk = TEMP_FAILURE_RETRY(accept(infd[i], NULL, 0));
			if (sk == -1) {
				FD_SET(infd[i], &rfds);
				continue;
			}
			/* IP sockopts will return -1 on UNIX-domain fd */
			setsockopt(sk, IPPROTO_IP, IP_TOS, &tos, 4);
			x = 1;
			setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, &x, 4);
			setsockopt(sk, SOL_SOCKET, SO_KEEPALIVE, &x, 4);
			setsockopt(sk, SOL_SOCKET, SO_OOBINLINE, &x, 4);
			l.l_onoff = 1;
			l.l_linger = 15;
			setsockopt(sk, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
			r = fcntl(sk, F_SETFL, O_NONBLOCK);
			if (r == -1) {
				FD_SET(infd[i], &rfds);
				continue;
			}
			if (sk > maxfd) maxfd = sk;
			FD_SET(sk, &rfds);
			remfd[i] = sk;
			wbufidx[i] = rbufremain[i] = wbufremain[i] = 0;
			if (!hup) xu_open(&xu, i, modes[i], bauds[i]);
			urgent |= (1 << i);
			urgent &= ~(1 << (i + 8));
			hasoddbyte &= ~(1 << i);
			overflowing &= ~(1 << i);
		} else {
			FD_SET(infd[i], &rfds);
		}
	}

	if (terminate) {
		xu_reset(&xu);
		if (is_cavium()) sbusunlock();
		if (is_mx28()) nbusunlock();
		for (i = 0; i < nchan; i++) {
			fcntl(infd[i], F_SETFL, 0);
			TEMP_FAILURE_RETRY(close(infd[i]));
			if (remfd[i] != -1) {
				fcntl(remfd[i], F_SETFL, 0);
				TEMP_FAILURE_RETRY(close(remfd[i]));
			}
		}
		return 0;
	}

	if (hup) {
		int nchan_o;
		hup = 0;
		nchan_o = nchan;
		sbus_shm[1] = nchan = xu_reset(&xu);
		if (nchan > 8) nchan = 0;
		/* If xuart core shrunk, close old sockets */
		if (nchan < nchan_o) for (i = nchan; i < nchan_o; i++) {
			struct linger lx;
			lx.l_onoff = 1;
			lx.l_linger = 0;
			fcntl(infd[i], F_SETFL, 0);
			TEMP_FAILURE_RETRY(close(infd[i]));
			if (remfd[i] != -1) {
				setsockopt(remfd[i], SOL_SOCKET, SO_LINGER, 
				  &lx, sizeof(lx));
				fcntl(remfd[i], F_SETFL, 0);
				TEMP_FAILURE_RETRY(close(remfd[i]));
			}
			/* Remove old FDs */
			FD_CLR(infd[i], &rfds);
		} else if (nchan > nchan_o) for (i = nchan_o; i < nchan; i++) {
			int r, sk, x = 1;
			/* TCP server socket */
			sa.sin_port = htons(7350 + i);
			sk = socket(PF_INET, SOCK_STREAM, 0);
			setsockopt(sk, SOL_SOCKET, SO_REUSEADDR, &x, 4);
			assert(sk != -1);
			r = bind(sk, (struct sockaddr *)&sa, sizeof(sa));
			if (r) {
				perror("bind");
				return 1;
			}
			r = listen(sk, 1);
			assert(r != -1);
			r = fcntl(sk, F_SETFL, O_NONBLOCK);
			assert(r != -1);
			if (sk > maxfd) maxfd = sk;
			FD_SET(sk, &rfds);
			infd[i] = sk;
	
			remfd[i] = -1;
			wbufremain[i] = rbufremain[i] = 0;
			strncpy(modes[i], opt_mode, 32);
			bauds[i] = opt_baudrate;
			rlw[i] = 1;
			cmdbuf[i][0] = 0;
		}
		if (nchan == 0) return 0;

		/* Re-open XUARTs */
		for (i = 0; i < nchan; i++) if (remfd[i] != -1) 
		  xu_open(&xu, i, modes[i], bauds[i]);
	}

	goto superloop_start;

	return 0;
}


static int do_client(void) {
	struct termios tios;
	struct sockaddr_in sa;
	struct hostent *he;
	fd_set rfds, wfds;
	int sk, r, x, tos = IPTOS_LOWDELAY;
	int pfd, pfdi, pfdo, maxfd, isapty;
	char *p;
	char wbuf[2048], rbuf[2048];
	char nbuf[32];
	int rbufremain, wbufremain, ridx, widx;
	struct linger lx;

	p = opt_port;
	if (*p == '/') {
		sk = open(p, O_RDWR|O_NOCTTY);
		assert(sk != -1);
		goto skipsocket;
	}
	/* TCP socket, --port opt must be in form hostname:port */
	while (*p != ':' && *p != 0) p++;
	assert(*p == ':');
	*p = 0;
	p++;

	sa.sin_family = AF_INET;
	he = gethostbyname(opt_port);
	assert(he != NULL);
	memcpy(&sa.sin_addr, he->h_addr_list[0], he->h_length);
	sa.sin_port = htons(strtoul(p, NULL, 0));
	assert(sa.sin_port != 0);
	sk = socket(PF_INET, SOCK_STREAM, 0);
	assert(sk != -1);
	lx.l_onoff = 1;
	lx.l_linger = 15;
	setsockopt(sk, SOL_SOCKET, SO_LINGER, &lx, sizeof(lx));
	r = connect(sk, (struct sockaddr *)&sa, sizeof(sa));

	if (r) {
		perror(opt_port);
		return 1;
	}

	if (opt_baudrate_set || opt_mode_set) {
		if (!opt_baudrate_set) opt_baudrate = 0;
		if (strlen(opt_mode) > 31) opt_mode[31] = 0;
		if (opt_mode_set) r = snprintf(nbuf, sizeof(nbuf), 
		  "%d@%s", opt_baudrate, opt_mode);
		else r = snprintf(nbuf, sizeof(nbuf), "%d", opt_baudrate);
		setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, &x, 4);
		x = send(sk, nbuf, 1, MSG_OOB);
		assert (x == 1);
		x = write(sk, &nbuf[1], r);
		assert (x == r);
		TEMP_FAILURE_RETRY(close(sk));
		sk = socket(PF_INET, SOCK_STREAM, 0);
		r = connect(sk, (struct sockaddr *)&sa, sizeof(sa));
		setsockopt(sk, SOL_SOCKET, SO_LINGER, &lx, sizeof(lx));
	}

	if (r) {
		perror(opt_port);
		return 1;
	}

	setsockopt(sk, IPPROTO_IP, IP_TOS, &tos, 4);
	x = 1;
	setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, &x, 4);

skipsocket:
	if (opt_test) return do_test(sk);

	r = fcntl(sk, F_SETFL, O_NONBLOCK);
	assert(r != -1);
	bzero(&tios, sizeof(tios));
	tios.c_cflag |= CS8 | CREAD | CLOCAL ;
	tios.c_cc[VTIME] = 0;
	tios.c_cc[VMIN] = 1;
	cfsetospeed(&tios, B9600);
	cfsetispeed(&tios, B0);

	if (opt_server) {
		/* background service a PTY */
		int slave, ret;
		ret = openpty(&pfd, &slave, NULL, &tios, NULL);
		assert(ret == 0);
		fprintf(stderr, "ttyname=%s\n", ttyname(slave));
		daemon(0, 0);
		pfdi = pfdo = pfd;
		isapty = 1;
	} else if (opt_ptyargc) {
		/* Invoke sub-program and connect to XUART */
		if (forkpty(&pfd, NULL, &tios, NULL) == 0) {
			/* child */
			execvp(opt_ptyargv[0], opt_ptyargv);
			assert(0); /* shouldn't reach */
		} 
		pfdi = pfdo = pfd;
		isapty = 1;
	} else {
		/* Connect stdin/stdout to socket */
		pfdi = 0;
		pfdo = 1;
		isapty = 0;
	}

	r = fcntl(pfdi, F_SETFL, O_NONBLOCK);
	assert(r != -1);
	r = fcntl(pfdo, F_SETFL, O_NONBLOCK);
	assert(r != -1);

	signal(SIGPIPE, SIG_IGN);

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_SET(sk, &rfds);
	FD_SET(pfdi, &rfds);
	maxfd = sk > pfdo ? sk : pfdo;
	rbufremain = wbufremain = ridx = widx = 0;
	for(;;) {
		int n;
		select(maxfd + 1, &rfds, &wfds, NULL, NULL);
		/* Data from socket to PTY */
		if (FD_ISSET(pfdo, &wfds)) {
			n = write(pfdo, &rbuf[ridx], rbufremain);
			if (n == -1) {
				if (errno != EAGAIN && errno != EINTR) break;
				n = 0;
			}
			rbufremain -= n;
			ridx += n;
		} else if (FD_ISSET(sk, &rfds)) {
			rbufremain = read(sk, rbuf, sizeof(rbuf));
			if (rbufremain == -1) {
				if (errno != EAGAIN && errno != EINTR) break;
				rbufremain = 0;
			} else if (rbufremain == 0) break;
			n = write(pfdo, rbuf, rbufremain);
			if (n == -1) {
				if (errno != EAGAIN && errno != EINTR) break;
				n = 0;
			}
			rbufremain -= n;
			ridx = n;
		}
		/* Linux has a PTY layer bug-- if you fill the pipe from PTM
		 * to PTS and go to sleep in select() waiting for the PTS to
		 * read pending data to make more room for the PTM to send 
		 * data, we never get woken up.  (Waiting for PTM fd to be 
		 * "writeable" after it returns a short write or EAGAIN)
		 * Furthermore, the PTS never sees the enqueued ~64kbytes data 
		 * until it sends a single byte to the PTM.
		 *
		 * As a result, if we are using PTYs and receive EAGAIN or a 
		 * short write to the PTM fd, we throw the XUART RX data away.
		 */ 
		if (rbufremain && !isapty) {
			FD_SET(pfdo, &wfds);
			FD_CLR(sk, &rfds);
		} else {
			FD_SET(sk, &rfds);
			FD_CLR(pfdo, &wfds);
		}

		/* Data from PTY to socket */
		if (FD_ISSET(sk, &wfds)) {
			n = write(sk, &wbuf[widx], wbufremain);
			if (n == -1) {
				if (errno != EAGAIN && errno != EINTR) break;
				n = 0;
			}
			wbufremain -= n;
			widx += n;
		} else if (FD_ISSET(pfdi, &rfds)) {
			wbufremain = read(pfdi, wbuf, sizeof(wbuf));
			if (wbufremain == -1) {
				if (errno != EAGAIN && errno != EINTR) break;
				wbufremain = 0;
			} else if (wbufremain == 0) break;
			n = write(sk, wbuf, wbufremain);
			if (n == -1) {
				if (errno != EAGAIN && errno != EINTR) break;
				n = 0;
			}
			wbufremain -= n;
			widx = n;
		} 
		if (wbufremain) {
			FD_SET(sk, &wfds);
			FD_CLR(pfdi, &rfds);
		} else {
			FD_SET(pfdi, &rfds);
			FD_CLR(sk, &wfds);
		}
	}

	fcntl(sk, F_SETFL, 0);
	TEMP_FAILURE_RETRY(close(sk));

	return 0;
}


static int do_test(int fd) {
	int i, r, j;
	struct timeval tv1, tv2;
	char dummy = 'U';
	struct sigaction sa;
	int nbits, nbits_possible, nbits_idle;
	float nbits_latency;

	sa.sa_handler = alarmsig;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGALRM, &sa, NULL);
	alarm(1);
	r = write(fd, &dummy, 1);
	if (r != 1) {
		fprintf(stderr, "loopback_test_ok=0\n");
		return 1;
	}
	r = read(fd, &dummy, 1);
	if (r != 1 || dummy != 'U') {
		fprintf(stderr, "loopback_test_ok=0\n");
		return 1;
	}
	alarm(0);
	fprintf(stderr, "loopback_test_ok=1\n");
	alarmed = 0;

	/* Save 10 seconds if doing a production test */
	if (getenv("TS_PRODUCTION") != NULL) return 0;

        alarm(10);
	gettimeofday(&tv1, NULL);
	for (i = 0; i < 10000 && !alarmed; i++) {
		r = write(fd, &dummy, 1);
		if (r != 1) break;
		r = read(fd, &dummy, 1);
		if (r != 1) break;
		assert(dummy == 'U');
	}
	gettimeofday(&tv2, NULL);
	alarm(0);
	j = (tv2.tv_sec - tv1.tv_sec) * 1000;
	j += tv2.tv_usec / 1000 - tv1.tv_usec / 1000;
	fprintf(stderr, "latency_test_tps=%d\n", (i * 1000) / j);
	nbits = 10 * (i * 2);
	nbits_possible = 115200 * j / 1000;
	nbits_idle = nbits_possible - nbits;
	nbits_latency = ((float)nbits_idle / 115200. / ((float)i * 2.));
	fprintf(stderr, "latency_us=%.1f\n", nbits_latency * 1000000.);
	return 0;
}


static int do_proxy(char *dev) {
	struct sockaddr_in sa;
	int n, r, listenfd, x=1, maxfd, i, j, devfd, devbuflen=0, nc, spc=2;
	char *devbuf = NULL;
	fd_set rfds, wfds;
	int *remfd, *buflen;
	char **buf, *urgent;
	struct termios tios;
	char tmpbuf[4096];
	ssize_t nr, nw;


	if (!dev) {
		/* background service a PTY */
		int slave;
		bzero(&tios, sizeof(tios));
		tios.c_cflag |= CS8 | CREAD | CLOCAL ;
		tios.c_cc[VTIME] = 0;
		tios.c_cc[VMIN] = 1;
		cfsetospeed(&tios, B9600);
		cfsetispeed(&tios, B0);
		r = openpty(&devfd, &slave, NULL, &tios, NULL);
		assert(r == 0);
		fprintf(stderr, "ttyname=%s\n", ttyname(slave));
		goto do_server_socket;
	} 
	devfd = open(dev, O_RDWR|O_NONBLOCK|O_NOCTTY);
	if (devfd == -1) {
		perror("open");
		return 1;
	}
#ifdef __linux__
	{
		struct serial_struct ss; 
		ioctl(devfd, TIOCGSERIAL, &ss);
		ss.flags |= ASYNC_LOW_LATENCY;
		ioctl(devfd, TIOCSSERIAL, &ss);
	}
#endif
	r = tcgetattr(devfd, &tios);
	assert(r == 0);
	cfmakeraw(&tios);
	if (opt_baudrate_set) {
		speed_t s;
		if (opt_baudrate >= 230400) s = B230400;
		else if (opt_baudrate >= 115200) s = B115200;
		else if (opt_baudrate >= 57600) s = B57600;
		else if (opt_baudrate >= 38400) s = B38400;
		else if (opt_baudrate >= 19200) s = B19200;
		else if (opt_baudrate >= 9600) s = B9600;
		else if (opt_baudrate >= 4800) s = B4800;
		else if (opt_baudrate >= 2400) s = B2400;
		else if (opt_baudrate >= 1200) s = B1200;
		else if (opt_baudrate >= 600) s = B600;
		else s = B300;
		cfsetspeed(&tios, s);
	}
	if (opt_mode_set) {
		tios.c_cflag &= ~CSIZE;
		if (opt_mode[0] == '8') tios.c_cflag |= CS8;
		else tios.c_cflag |= CS7;

		if (opt_mode[1] == 'n' || opt_mode[1] == 'N')
		  tios.c_cflag &= ~PARENB;
		else if (opt_mode[1] == 'e' || opt_mode[1] == 'E') {
			tios.c_cflag &= ~PARODD;
			tios.c_cflag |= PARENB;
		} else tios.c_cflag |= PARENB|PARODD;

		if (opt_mode[2] == '2') tios.c_cflag |= CSTOPB;
		else tios.c_cflag &= ~(CSTOPB);
	}
	tios.c_cflag |= CLOCAL;
	r = tcsetattr(devfd, TCSANOW, &tios);
	assert(r == 0);

do_server_socket:
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = opt_bind.s_addr;

	/* TCP server socket, random listener port */
	sa.sin_port = 0;
	listenfd = socket(PF_INET, SOCK_STREAM, 0);
	assert(listenfd > 2);
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &x, 4);
	assert(listenfd != -1);
	r = bind(listenfd, (struct sockaddr *)&sa, sizeof(sa));
	if (r) {
		perror("bind");
		return 1;
	}
	r = listen(listenfd, 5);
	assert(r != -1);
	r = fcntl(listenfd, F_SETFL, O_NONBLOCK);
	assert(r != -1);

	x = sizeof(struct sockaddr_in);
	r = getsockname(listenfd, (struct sockaddr *)&sa, (socklen_t *)&x);
	assert(r == 0);
	fprintf(stderr, "tcp_port=%d\n", ntohs(sa.sin_port));

	buf = malloc(sizeof(*buf) * spc);
	remfd = malloc(sizeof(*remfd) * spc);
	urgent = malloc(sizeof(*urgent) * spc);
	buflen = malloc(sizeof(*buflen) * spc);
	for (i = 0; i < spc; i++) remfd[i] = -1;
	nc = 0;

	daemon(0, 0);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

superloop:
	FD_ZERO(&rfds);
	FD_ZERO(&wfds);
	FD_SET(listenfd, &rfds);
	for (maxfd = listenfd, i = j = 0; j < nc; i++) if (remfd[i] != -1) {
		if (devbuflen == 0) FD_SET(remfd[i], &rfds);
		if (remfd[i] > maxfd) maxfd = remfd[i];
		if (buflen[i]) FD_SET(remfd[i], &wfds);
		j++;
	}
	FD_SET(devfd, &rfds);
	if (devbuflen) FD_SET(devfd, &wfds);
	n = select(maxfd + 1, &rfds, &wfds, NULL, NULL);
	if (n == -1 && errno == EINTR) goto superloop;

	if (FD_ISSET(devfd, &rfds)) { /* New data to read from tty */
		nr = read(devfd, tmpbuf, 4096);
		assert(nr != 0); /* EOF on tty dev?? */

		/* Write to all sockets */
		for (i = j = 0; j < nc; i++) if (remfd[i] != -1) {
			j++;
			if (buflen[i]) continue;
			nw = write(remfd[i], tmpbuf, nr);
			if (nw != nr) {
				buf[i] = malloc(4096);
				memcpy(buf[i], &tmpbuf[nw], nr - nw);
				buflen[i] = nr - nw;
			} 
		} 
		n--;
	}

	if (FD_ISSET(devfd, &wfds)) { /* Flush tty output buffer */
		nw = write(devfd, devbuf, devbuflen);
		if (nw == -1 && (errno == EINTR || errno == EAGAIN)) nw = 0;
		else assert(nw != -1);
		if (nw == devbuflen) {
			free(devbuf);
			devbuflen = 0;
		} else {
			devbuflen -= nw;
			memmove(devbuf, &devbuf[nw], devbuflen);
		}
		n--;
	}

	for (i = j = 0; n > 0 && j < nc; i++) if (remfd[i] != -1) {
		j++;
		if (FD_ISSET(remfd[i], &wfds)) {
			nw = write(remfd[i], buf[i], buflen[i]);
			if (nw == -1 && (errno == EINTR || errno == EAGAIN))
			  nw = 0; else assert(nw != -1);
			if (nw == buflen[i]) {
				buflen[i] = 0;
				free(buf[i]);
			} else {
				buflen[i] -= nw;
				memmove(buf[i], &buf[i][nw], buflen[i]);
			}
			n--;
		}

		if (FD_ISSET(remfd[i], &rfds)) {
			n--;
			if (devbuflen != 0) continue;
			nr = read(remfd[i], tmpbuf, 4096);
			if (urgent[i] == 0) {
				/* Check OOB */
				r = ioctl(remfd[i], SIOCATMARK, &x);
				if (r == -1) nr = 0; /* EOF */
				if (!x) urgent[i] = 1;
				else urgent[i] = 2;
			}
			if (nr == 0) { /* EOF */
				TEMP_FAILURE_RETRY(close(remfd[i]));
				if (buflen[i]) free(buf[i]);
				remfd[i] = -1;
				nc--;
			} else if (urgent[i] == 2) continue; /* Cmd ignored */
			nw = write(devfd, tmpbuf, nr);
			if (nw == -1 && 
			  (errno == EAGAIN || errno == EINTR)) nw = 0;
			assert(nw != -1);
			if (nw != nr) {
				devbuf = malloc(4096);
				memcpy(devbuf, &tmpbuf[nw], nr - nw);
				devbuflen = nr - nw;
			}
		}

	}

	if (n && FD_ISSET(listenfd, &rfds)) { /* New connection requests... */
		int sk, tos = IPTOS_LOWDELAY;
		struct linger l;
		sk = TEMP_FAILURE_RETRY(accept(listenfd, NULL, 0));
		if (sk == -1) goto superloop;
		setsockopt(sk, IPPROTO_IP, IP_TOS, &tos, 4);
		x = 1;
		setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, &x, 4);
		setsockopt(sk, SOL_SOCKET, SO_KEEPALIVE, &x, 4);
		setsockopt(sk, SOL_SOCKET, SO_OOBINLINE, &x, 4);
		l.l_onoff = 1;
		l.l_linger = 15;
		setsockopt(sk, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
		r = fcntl(sk, F_SETFL, O_NONBLOCK);
		if (r == -1) goto superloop;
		for (i = 0; i < spc; i++) if (remfd[i] == -1) break;
		if (i == spc) {
			spc *= 2;
			buf = realloc(buf, sizeof(*buf) * spc);
			remfd = realloc(remfd, sizeof(*remfd) * spc);
			urgent = realloc(urgent, sizeof(*urgent) * spc);
			buflen = realloc(buflen, sizeof(*buflen) * spc);
			for (j = i; j < spc; j++) remfd[j] = -1;
		} 
		remfd[i] = sk;
		buf[i] = NULL;
		urgent[i] = buflen[i] = 0;
		nc++;
	}

	if (nc < (spc/4)) { /* compress/defragment */
		int *nremfd, *nbuflen, nspc = spc/2;
		char **nbuf, *nurgent;

		nbuf = malloc(sizeof(*nbuf) * nspc);
		nremfd = malloc(sizeof(*nremfd) * nspc);
		nurgent = malloc(sizeof(*nurgent) * nspc);
		nbuflen = malloc(sizeof(*nbuflen) * nspc);
		for (i = 0; i < nspc; i++) nremfd[i] = -1;
		for (j = i = 0; i < spc; i++) if (remfd[i] != -1) {
			nremfd[j] = dup(remfd[i]);
			TEMP_FAILURE_RETRY(close(remfd[i]));
			nbuflen[j] = buflen[i];
			nurgent[j] = urgent[i];
			nbuf[j++] = buf[i];
		}
		free(remfd);
		free(buflen);
		free(buf);
		free(urgent);
		remfd = nremfd;
		buflen = nbuflen;
		buf = nbuf;
		urgent = nurgent;
		spc = nspc;
	}

	goto superloop;
	return 0;
}
