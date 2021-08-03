#ifndef XUART_H
#define XUART_H

/* The struct xuartcore is the one and only significant data structure in the
 * XUART API.  Significant public members at a glance:
 * * xu_regstart - valid address to start of XUART IO space registers
 * * xu_memstart - valid address to start of XUART memory
 * * xu_osarg - used as first argument passed to callback functions
 * * xu_atomic_set - callback function that does an atomic test and set
 * * xu_maskirq - callback function called to mask/unmask the XUART irq
 * * xu_wait - callback function for thread-API specific blocking
 * * xu_signal - callback function for thread-API specific unblocking
 * * xu_rxbreak - callback function called when break detected
 * * xu_rxidle - callback function called when idle detected
 *
 * The rest of the data structure is for private, internal API usage only.
 *
 * The application code need only zero-out or set the above members before
 * initialization.  The rest of the data structure does not need any special
 * initialization not provided by xu_reset() (i.e. not necessary to bzero)
 *
 * The API is designed to still function correctly if no callbacks are set
 * though application code may have to pay special attention to threading
 * and locking requirements. (e.g. API becomes non-thread safe if no
 * callbacks implemented)
 */

struct xuartcore {
	/* xu_regstart must be initialized before calling xu_reset() and must
	 * be a pointer to the start of XUART IO space.  xu_memstart must
	 * point to the start of the XUART memory.
	 */
	size_t xu_regstart;
	size_t xu_memstart;

	/* xu_osarg is supplied as the first argument to all callbacks */
	void *xu_osarg;

	/* xu_atomic_set is called upon to atomically set the value and
	 * return the previous state of the value pointed to by the arg.  If
	 * this is NULL, a default, non-atomic version will be used and the
	 * XUART API calls should not be considered thread-safe.
	 */
	int (*xu_atomic_set)(int *);

	/* xu_maskirq is called to request masking/unmasking of the XUART
	 * IRQ.  This is to prevent asynchronous entry into the xu_irq()
	 * function when internal data structures are being manipulated.
	 * While IRQ's are unmasked, it is guaranteed xu_irq() is able to
	 * complete its function without blocking.  If NULL, and xu_irq()
	 * is called asynchronously from another thread preempted from
	 * another XUART API call, xu_atomic_set, xu_wait, and xu_signal MUST 
	 * be implemented as xu_irq() may end up having to sleep to wait
	 * for locks. 
	 *
	 * The function's second parameter is 1 to request masking, and 0
	 * to request unmasking.  The return value is the previous state
	 * of interrupt masking (1 - masked, 0 - unmasked);
	 */
	int (*xu_maskirq)(void *, int);

	/* xu_wait is called when the API is blocked waiting either for
	 * a hardware event or another thread to release a lock.  xu_signal is
	 * called to unblock another thread waiting in xu_wait().  The arg
	 * is a number from 0-24 representing the wait channel:
	 *
	 * 0 - RX wait channel for serial port #0
	 * 1 - RX wait channel for serial port #1
	 * ...
	 * 7 - RX wait channel for serial port #7
	 * 8 - TX wait channel for serial port #0
	 * 9 - TX wait channel for serial port #1
	 * ...
	 * 15 - TX wait channel for serial port #7
	 * 16-24 - internal mutex lock wait channels
	 *
	 * TX wait channels are slept on in xu_write() or xu_writec() whenever
	 * there is not enough buffer space to accept the user's full amount
	 * of TX bytes.  TX wait channels are also slept on in xu_draintx and
	 * xu_close to block until all pending TX data has been transmitted. 
	 * xu_open may also block here so that the receiver can be enabled 
	 * only after the baud rate has changed.
	 *
	 * RX wait channels are slept on in xu_read() and xu_readc() if there
	 * are not enough received bytes to completely satisfy the read
	 * request.
	 *
	 * xu_wait() may refuse to sleep on a given wait channel by returning
	 * -1.  In this way, client code can individual enable non-blocking
	 * modes for each serial channel in either the TX or RX direction.
	 * Also, the wait could have a time-out in which xu_wait will return -1
	 * after some time has elapsed without the wait channel having been
	 * signaled.
	 *
	 * If xu_wait is NULL, the XUART API always runs non-blocking and 
	 * xu_signal may then also be NULL.
	 *
	 * While being called upon to potentially sleep in xu_wait() on a 
	 * channel < 16, it is guaranteed no lock is held and the XUART IRQ
	 * will be unmasked.
	 *
	 * There may be multiple sleepers at a time on a given wait channel.
	 * It is ok to wake up all of them or only one of them when xu_signal
	 * is called.
	 *
	 * Wait channels 16-24 will never be slept on if there is never
	 * contention for internal locks.  Contention can be avoided if xu_irq()
	 * is only called when the IRQ is unmasked (as signaled by the 
	 * xu_maskirq() callback) and that no more than 1 thread is executing
	 * inside the XUART API functions at a time.  If called upon to wait
	 * on one of these wait channels, blocking cannot be refused (e.g.
	 * cannot return -1).  If blocking is refused on these channels or
	 * xu_wait is NULL, the API will purposefully attempt to crash by 
	 * derefencing NULL.
	 *
	 */
	int (*xu_wait)(void *, int);
	void (*xu_signal)(void *, int);


	/* xu_rxbreak() may be called from within the context of xu_readc()
	 * when a RX break is detected.  The first argument passed is the 
	 * xu_osarg, the second argument is the serial channel number, the
	 * third is the length of the break in bit times at the current
	 * baudrate, and the 4th is the index in the xu_readc buffer the 
	 * break immediately precedes. (e.g. 0 means before the first
	 * character, 1 means after the first character) If the function
         * returns -1, the rest of the xu_readc() is aborted and a short
         * read may be returned.
	 *
	 * xu_rxidle() is similar to the xu_rxbreak() callback except that it
	 * signifies a period of idle detected.
	 *
	 * This function will be called with no locks held.
	 */
	void (*xu_rxbreak)(void *, int, int, int);
	void (*xu_rxidle)(void *, int, int, int);

	int status;
	char privatespace[69936]; /* for buffers, etc... */
};

/* Public XUART API functions at a glance:
 * * xu_reset() -- resets and initializes FPGA core and xuartcore struct
 * * xu_open() -- initializes a particular serial channel
 * * xu_close() -- closes a particular serial channel
 * * xu_readc() -- reads characters (8-bits) from a particular channel
 * * xu_read() -- reads raw XUART RX works from channel
 * * xu_writec() -- writes characters to channel
 * * xu_write() -- write raw XUART TX opcodes to channel
 * * xu_stoptx() -- suspend/resume TX (without having to wait to drain buffers)
 * * xu_txbreak() -- initiate/terminate continuous serial BREAK condition
 * * xu_draintx() -- sleep until all TX completes
 * * xu_irq() -- client code should call this when XUART IRQ detected
 */

/* This is the first function that should be called.  It requires at the very
 * least that xu_regstart and xu_memstart members be setup ahead of time as
 * pointers to XUART io space and memory space, respectively.  Unutilized 
 * callback functions should be set to NULL.  xu_osarg is always the first
 * argument sent to callbacks (with the exception of xu_atomic_set) and should
 * also be set up before the xu_reset() call.
 *
 * xu_reset() returns the number of serial channels detected, or 0 if there
 * was no XUART core detected.
 */
int xu_reset(struct xuartcore *xu);


/* xu_open() initializes the given XUART serial channel baudrate and serial
 * parameters.  The "mode" argument is a string representing serial mode such
 * as "8n1" or "7e2" -- supported modes are listed below:
 *
 * * 8n1
 * * 8n2
 * * dmx - when in DMX mode, baudrate arg is not used (hardcoded 250 kbaud)
 * * 8e1
 * * 8o1
 * * 8e2
 * * 8o2
 * * 7n1
 * * 7n2
 * * 7e1
 * * 7o1
 * * 7e2
 * * 7o2
 * * 9n1 - in 9-bit mode, the character read/write routines xu_readc/xu_writec
 *   probably should not be used.
 *
 * If ",hwcts" is appended to the mode (e.g. "8n1,hwcts") hardware flow control
 * is used.  Returns -1 on error and 0 on success.
 */
int xu_open(struct xuartcore *xu, int uartn, const char *mode, int baudrate);


/* xu_writec (write characters) takes a byte array and tries to enqueue up 
 * to 'n' bytes for transmission.  If n is 0 or buf is NULL, nothing is
 * written, and the current number of TX bytes pending is returned.  Up to
 * 2 kbytes is enqueued in the XUART software API and 256 bytes can be 
 * enqueued in the hardware.  If there is not enough space to satisfy 
 * the request and TX blocking mode is enabled (see discussion on blocking
 * modes under the xu_wait() callback above), xu_writec will block until
 * all bytes are enqueued.  If TX is configured as non-blocking, xu_writec
 * will return however many bytes were successfully accepted which may be
 * zero.
 *
 * xu_write and xu_writec are not considered thread-safe if called from 
 * multiple threads for the same UART channel at the same time.
 *
 * xu_write is just like xu_writec except that it takes raw XUART TX opcodes
 * as described in the XUART hardware documentation.  This interface should
 * be used in >8 bit modes or when periods of timed breaks and idle
 * periods are necessary.
 */
int xu_writec(struct xuartcore *xu, int uartn, unsigned char *buf, int n);
int xu_write(struct xuartcore *xu, int uartn, unsigned short *buf, int n);


/* xu_readc returns up to "n" received bytes in "buf" and returns how many
 * were read.  If buf is NULL, xu_readc returns how many bytes are waiting
 * to be read without actually reading them.  The XUART API can buffer
 * 2 kbytes.  RX IRQ scheduling can be affected as well with this function
 * as a performance optimization according to the table below. 
 *
 * RX IRQ scheduling:
 * case 1 - passed a non-NULL buf arg to read function and ...
 *  case 1a - req. fully completed and ...
 *   case 1a1 - all chars from sw buf: retain prev. RX IRQ state.
 *   case 1a2 - needed chars from hw buf: delay IRQ as long as possible.
 *  case 1b - req. incomplete: no IRQ until would-be completion point
 * case 2 - passed a NULL buf: service hw and return num bytes waiting ...
 *  case 2a - n arg < 0: hardcode delay for -n chars
 *  case 2b - n arg > 0: n interpreted as next read op size "hint" ...
 *   case 2b1 - chars waiting >= n: delay IRQ as long as possible
 *   case 2b2 - chars waiting < n: delay to would-be completion point
 *
 * xu_read and xu_readc are not considered thread-safe if called from 
 * multiple threads for the same UART channel at the same time.
 *
 * xu_read is just like xu_readc except that it returns raw XUART RX words.
 * Refer to the XUART hardware documentation for further information.
 */
int xu_readc(struct xuartcore *xu, int uartn, unsigned char *buf, int n);
int xu_read(struct xuartcore *xu, int uartn, unsigned short *buf, int n);


/* xu_stoptx() either suspends TX ("stop" parameter == 1) or resumes TX
 * ("stop" parameter == 0).  The current byte being transmitted will
 * complete and possibly one more after that after being suspended.  Returns
 * 1 if the transmitter is currently shifting bits out, 0 if the transmitter
 * is idle, and -1 on error (uart not open).
 *
 * If the channel is closed while suspended, all pending TX data is canceled.
 */
int xu_stoptx(struct xuartcore *xu, int uartn, int stop);


/* xu_txbreak() either starts/cancels sending of a continuous break condition.
 * if "brk" == 1, current TX activity is suspended similar to xu_stoptx() and
 * followed by the continuous assertion of the break condition on the line.
 * This is not a timed break -- those must be sent with proper TX opcodes 
 * sent with xu_write().
 *
 * Returns 0 on success, or -1 on error.
 */ 
int xu_txbreak(struct xuartcore *xu, int uartn, int brk);


/* xu_draintx() will block the current thread until all pending TX is completed
 * provided TX blocking mode is enabled.  If TX blocking is not enabled,
 * xu_draintx() will only return the count of TX bytes remaining to be sent.
 *
 * Even with blocking enabled, draintx may still return a value of "1"
 * representing the very last byte as it shifts out of the transmitter. If
 * interested in synchronizing with the completion of all activity on the TX
 * line, xu_draintx() should be called in a loop until 0 is returned.
 */
int xu_draintx(struct xuartcore *xu, int uartn); 


/* xu_close() will disable a channel and block until all pending TX is
 * completed.  If TX is non-blocking, xu_close() will return a count of bytes
 * remaining for TX but still initiate XUART channel shutdown.  After closing,
 * xu_read() and xu_readc() may still be called to retreive pending RX bytes,
 * but upon re-opening, they will be lost.
 *
 * Returns 0 when closing is completed, -1 on error.
 */ 
int xu_close(struct xuartcore *xu, int uartn);


/* xu_irq() must be called by client code when the XUART asserts an IRQ.
 * It will return 0 if there was nothing to do, non-zero if new bytes were
 * RX/TX'ed, and -1 when an overflow is detected.
 *
 * If something was done, the return status is a 16 bit number:
 *   bits 15-8: TX activity (bit 8 -> channel 0)
 *   bits 7-0: RX activity (bit 0 -> channel 0)
 * This return status can be used in upper layer IRQ handlers to skip 
 * further processing of UART channels without activity.
 */
int xu_irq(struct xuartcore *xu);

#endif
