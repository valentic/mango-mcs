/*  Copyright 2013, Unpublished Work of Technologic Systems
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

#ifdef __linux__
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mvdmastream.h"

struct mvdmacore {
	size_t mvburst;
	size_t dmareg;
	int devmem;
	int devkmem;
	int chan;
};

static struct mvdmacore mvdma;

#define poke16(adr, dat) *(volatile unsigned short *)(adr) = (dat)
#define peek16(adr) (*(volatile unsigned short *)(adr))

static int (*svc_funcptr)(unsigned int a, unsigned int b, unsigned int c);;
static int svc_cb(unsigned int a, unsigned int b, unsigned int c) {
	return svc_funcptr(a, b, c);	
}
int svccall(void *funcptr, unsigned int a, unsigned int b, unsigned int c) {
	static int installed;
	static int unused_sc[4] = {17, 18, 31, 32};

	assert(mvdma.chan >= 0 && mvdma.chan <= 3);

	if (!installed) {
		int ret, devkmem = -1;
		unsigned int adr;
		size_t f = (size_t)svc_cb;
		char sym[256], x;
		FILE *k;
		off_t oret;
		devkmem = open("/dev/kmem", O_RDWR);
		assert(devkmem != -1);
		k = fopen("/proc/kallsyms", "r");
		assert(k != NULL);

		do {
			ret = fscanf(k, "%x %c %s", &adr, &x, sym);
			if (strcmp(sym, "sys_call_table") == 0 ||
			  strcmp(sym, "sys_oabi_call_table") == 0) {
				/* obsolete ARM syscall */
				oret = lseek(devkmem,
				            adr + (unused_sc[mvdma.chan]*4),
					    SEEK_SET);
				assert(oret != (off_t)-1);
				ret = write(devkmem, &f, 4);
				assert(ret == 4);
				if (++installed == 2) break;
			}
		} while (ret >= 3);
		fclose(k);
		close(devkmem);
	}

	svc_funcptr = funcptr;
	return syscall(unused_sc[mvdma.chan], a, b, c);
}


/* Read burst routine from FPGA to CPU on a TS-4710 Marvell PXA16x */
static void burst(void *adr, unsigned int nbytes) {
#ifdef __arm__
	unsigned int cpsr;
	size_t eadr = (size_t)adr + nbytes;

	/* Check if we're in SVC mode, if not, use svccall() */            
	asm volatile ( "mrs %0, CPSR\n\t" : "=r"(cpsr) : );                
	if ((cpsr & 0x1f) == 0x10) {
		svccall(burst, (size_t)adr, nbytes, 0);
		return;
	}

	asm volatile (
		"1: ldmia %2, {r3-r10}\n\t" /* Cache line fill */
		"mcr p15, 0, %2, c7, c6, 1\n\t" /* Invalidate L1-d via MVA */
		"mcr p15, 1, %2, c7, c7, 1\n\t" /* L2 invalidate via MVA */
		"stmia %0!, {r3-r10}\n\t"
		"cmp %1, %0\n\t"
		"bne 1b\n\t"
		: "+r"(adr) : "r"(eadr), "r"(mvdma.mvburst) 
		: "r3","r4","r5","r6","r7","r8","r9","r10","cc","memory"
	);
#endif
}


/* Write burst routine from FPGA to CPU on a TS-4710 Marvell PXA16x */
static void burst_wr(void *adr, unsigned int nbytes) {
#ifdef __arm__
	unsigned int cpsr;
	size_t eadr = (size_t)adr + nbytes;

	/* Check if we're in SVC mode, if not, use svccall() */            
	asm volatile ( "mrs %0, CPSR\n\t" : "=r"(cpsr) : );                
	if ((cpsr & 0x1f) == 0x10) {
		svccall(burst_wr, (size_t)adr, nbytes, 0);
		return;
	}


	asm volatile (
		"1: ldmia %0!, {r3-r10}\n\t" /* Cache line fill */
		"stmia %2, {r3-r10}\n\t" /* Cache line write */
		"mcr p15, 0, %2, c7, c14, 1\n\t" /* Clean/inv L1-d via MVA */
		"mcr p15, 1, %2, c7, c15, 1\n\t" /* Clean/inv L2 via MVA */
		"mcr p15, 0, %2, c7, c10, 4\n\t" /* drain WB */
		"mrc p15, 0, r3, c2, c0, 0\n\t" /* "cpwait" operation 1/3... */
		"mov r3, r3\n\t" /* 2/3... */
		"sub pc, pc, #4\n\t" /* 3/3... */
		"cmp %1, %0\n\t"
		"bne 1b\n\t"
		: "+r"(adr) : "r"(eadr), "r"(mvdma.mvburst) 
		: "r3","r4","r5","r6","r7","r8","r9","r10","cc"
	);
#endif
}
/*
 * Register map:
 * base + 0x0: DMA control register (R/W)
 *   bits 11:0 - DMA length (number of transfers, not necessarily bytes)
 *   bit 12 - DMA direction (0 - wbm1 reads, 1 - wbm2 reads)
 *   bits 15:13 - wbm2 address increment per transfer
 *   bits 18:16 - wbm1 address increment per transfer
 *   bits 20:19 - DMA transfer size 
 *     0 - 1 byte
 *     1 - 2 bytes (halfwords)
 *     2 - 4 bytes (words)
 *     3 - reserved
 *   bits 29:21 - reserved
 *   bit 30 - DRQ1 input (wbm1 is ready for DMA)
 *   bit 31 - DRQ2 input (wbm2 is ready for DMA)
 * base + 0x4: wbm1 address (R/W) - must be transfer size aligned
 * base + 0x8: wbm2 address (R/W) - must be transfer size aligned
 *
 * WBM 2 in this case is connected to blockram
 */
int mvdmastream(unsigned int dest,
                       char *buf,
		       unsigned int len,
		       int dir,
		       int incr) {
	unsigned int x, reg;

	assert(((size_t)buf & 0x3) == 0); /* Better be 4-byte aligned... */

	while (len > 0x1f00) { /* Break it up in 0x1f00 byte chunks */
		mvdmastream(dest, buf, 0x1f00, dir, incr);
		buf += 0x1f00;
		len -= 0x1f00;
		dest += 0x1f00;
	}

	len = len >> 1;
	assert((len & 0xf) == 0); // Bursts must be 16 words
	poke16(mvdma.dmareg + 0x2, 0x8); // transfer size
	poke16(mvdma.dmareg + 0x8, dest & 0xffff);
	poke16(mvdma.dmareg + 0xa, dest >> 16);

	reg = len;
	if (incr) reg |= 0x4000;

	if (!dir) {
		/* Start wb_dma feed to lattice_fifo_dc from the blockram: */
		poke16(mvdma.dmareg, reg|0x1000); 
		while (len) {
			x = len - (peek16(mvdma.dmareg) & 0xfff);
			if (x >= 16) { /* We pull out 32 bytes at a time */
				len -= (x & ~0xf);
				burst(buf, (x & ~0xf)<<1);
				buf += (x & ~0xf)<<1;
			} 
		}
	} else {
		assert(len >= 128 && (len & 0x7f) == 0);
		/* Start wb_dma feed from lattice_fifo_dc to the blockram: */
		poke16(mvdma.dmareg, reg); 

		for (x = 0; x < len; buf += 256) {
			burst_wr(buf, 256);
			x += 128;
			while ((x - (len - (peek16(mvdma.dmareg) & 0xfff))) > 64);
		}
		while ((peek16(mvdma.dmareg) & 0xfff)); /* wait for 100% complete */
	}

	return 0;
}

static size_t get_ttbl() {
#ifdef __arm__                                                             
	size_t ret;                                                        
	unsigned int cpsr;                                                 
	                                                                   
	/* Check if we're in SVC mode, if not, use svccall() */            
	asm volatile ( "mrs %0, CPSR\n\t" : "=r"(cpsr) : );                
	if ((cpsr & 0x1f) == 0x10) return svccall(get_ttbl, 0, 0, 0);      
	                                                                   
	asm volatile ( 
		"mrc p15, 0, %0, c2, c0, 0\n\t" 
		"mcr p15, 0, r0, c7, c14, 0\n\t" /* clean/inv L1 dcache */
		"mcr p15, 0, r0, c7, c10, 4\n\t" /* drain wbuf */
		"mcr p15, 1, r0, c7, c11, 0\n\t" /* clean L2 */
		"mcr p15, 0, r0, c8, c7, 0\n\t" /* inv TLBs */
		: "=r"(ret) :
	);    
	return ret & ~0x3fff; 
#else
	return 0;
#endif                                                                     
}                                                                          

static void *mmap_cacheable(void *adr, size_t len, int prot, int flags,
  int fd, off_t offset) {
	unsigned long *l1tbl, *l2tbl, *l2ent, *l1ent;
	size_t ret;

	assert(len == 4096); /* For now... */
	ret = (size_t)mmap(adr, len, prot, flags, fd, offset);

	l1tbl = (void *)mmap(0, 16384, PROT_READ|PROT_WRITE, 
	  MAP_SHARED, mvdma.devmem, get_ttbl());

	l1ent = &l1tbl[ret >> 20];
	assert((*l1ent & 0x3) == 1); /* Assume coarse page table */
	l2tbl = (void *)mmap(0, 4096, PROT_READ|PROT_WRITE, 
	  MAP_SHARED, mvdma.devmem, *l1ent & ~0xfff);
	l2ent = (void *)((size_t)l2tbl + (*l1ent & 0xc00));
	munmap(l1tbl, 16384);
	l2ent = &l2ent[(ret & 0xff000) >> 12];
	*l2ent |= 0xc; /* Mark cacheable/bufferable */
	get_ttbl(); /* Side-effect of L1/L2 cache cleaning */
	munmap(l2tbl, 4096);

	return (void *)ret;
}

/* Page table walk */
static size_t virt2phys(size_t virt) {
	static unsigned long *l1tbl;	
	static unsigned long *l2tbl;
	static int l1idx_last;
	int l1idx = virt >> 20;
	unsigned long l1desc;
	unsigned long l2desc;
	int fault = 0;

	if (virt == 0) { /* arg of 0 has the side-effect of refresh */
		if (l1tbl) munmap(l1tbl, 16384);
		if (l2tbl) munmap((void *)((size_t)l2tbl & ~0xfff), 4096);
		l1tbl = l2tbl = NULL;
		return 0;
	}

	if (l1tbl == NULL) {
		l1tbl = (void *)mmap(0, 16384, PROT_READ|PROT_WRITE, 
		  MAP_SHARED, mvdma.devmem, get_ttbl());
		l1idx_last = -1;
	}

	if (l1idx_last != l1idx) {
		l1desc = l1tbl[l1idx];
		switch (l1desc & 0x3) {
		case 0x1: /* Coarse page table */
			if (l2tbl != NULL) 
			  munmap((void *)((size_t)l2tbl & ~0xfff), 4096);
			l2tbl = (void *)mmap(0, 4096, PROT_READ|PROT_WRITE, 
			  MAP_SHARED, mvdma.devmem, l1desc & ~0xfff);
			l2tbl = (void *)((size_t)l2tbl + (l1desc & 0xc00));
			l1idx_last = l1idx;
			break;

		case 0x2: /* Section */
			return ((virt & 0xfffff) | (l1desc & ~0xfffff));

		case 0x0: /* Fault */
			assert(fault++ == 0);
			virt2phys(0);
			return virt2phys(virt);

		case 0x3: /* Fine page table (not used in Linux/BSD) */
			assert(0);
		};
	}

	l2desc = l2tbl[(virt & 0xff000) >> 12];

	return ((virt & 0xfff) | (l2desc & ~0xfff));
}

/* Refresh page tables */
static void refresh_pgtbl() { virt2phys(0); }

void mvdmainit(int chan) {
	mvdma.devmem = open("/dev/mem", O_RDWR|O_SYNC);
	assert(mvdma.devmem != -1);
	mvdma.devkmem = open("/dev/kmem", O_RDWR|O_SYNC);
	assert(mvdma.devkmem != -1);

	mvdma.chan = chan;
	refresh_pgtbl();
	mvdma.mvburst = (size_t)mmap_cacheable(0, 4096, 
	  PROT_READ | PROT_WRITE, MAP_SHARED, mvdma.devmem, 0x80004000);
	mvdma.mvburst += 0xe20 + 0x40 * chan;
	mvdma.dmareg = (size_t)mmap(0, 4096, 
	  PROT_READ | PROT_WRITE, MAP_SHARED, mvdma.devmem, 0x80004000);
	mvdma.dmareg += 0xe00 + 0x40 * chan;


	// This is to empty any old garbage in the read FIFO, which should not be necessary.
	//while (peek16(ts47xxregs+0xe80)&0xfff) burst(bufpg, 4096); // XXX
}

#ifdef DMA_TESTBENCH

int main(void) {
	char buf[0x9000];
	char *bufpg;
	int i;

	bufpg = (char *)(((size_t)buf + 4095) & ~0xfff);
	mvdmainit(2);

	// move 32KB from stdin to bufpg
	for(i=0; i<0x8000; i++) bufpg[i] = getchar();

	// move 32KB from bufpg to blockram
	mvdmastream(0x10000, bufpg, 0x8000, 1);

	// verify bufpg = blockram

	// zero out bufpg
	for(i=0; i<0x8000; i++) bufpg[i] = 0xff;

	// move 32KB from blockram to bufpg
	mvdmastream(0x10000, bufpg, 0x8000, 0);

	// verify bufpg = blockram

	// move 32KB from bufpg to stdout
	for(i=0; i<0x8000; i++) putchar(bufpg[i]);

	return 0;
}

#endif

