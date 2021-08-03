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

#ifndef MVDMASTREAM_H
#define MVDMASTREAM_H
/* Before using mvdmastream(), you must call mvdmainit once and only once.
 * The argument to mvdmainit is the DMA channel number.  Valid channel numbers
 * are 0 to 3.  Channel 0 is connected to the SD core and is normally used by
 * sdctl.  By default, channel 1 is connected to the SPI flash core.  Channels
 * 2 and 3 are open for custom uses.  In the stock FPGA, channel 3 does not
 * exist and channel 2 arbitrates for access to all 100MHz slaves.
 *
 * Only one process at any time can use a particular channel.
 *
 * One process controlling two DMA channels is not supported.
 */
void mvdmainit(int);

/* mvdmastream(dest, buf, len, dir, incr);
 * This is a function to execute a streaming transfer from the FPGA to main
 * memory or from main memory to the FPGA.
 * Arguments:
 * unsigned int dest: This is the address in FPGA space.  For some channels,
 * all bus cycles go to one register which is hard coded in the FPGA, so this
 * argument is ignored.
 * char *buf: Pointer to main memory to or from which you are transferring.
 * unsigned int len: Number of bytes to transfer.  MUST be a multiple of 32.
 * int dir: 0 = read, 1 = write
 * int incr: 1 = Normal memory copy operation, 0 = sit on one FPGA register
 *
 * On the FPGA side, registers are 16 bits, so a transfer with incr=0 will
 * pipe 16 bit words in or out.
 */
int mvdmastream(unsigned int, char *, unsigned int, int, int);

#endif

