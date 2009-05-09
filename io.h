enum {
	Intrbase =	0xf1000000,
	AddrIntr =	Intrbase+0x20200, /* xxx correct? */
	AddrTimer =	Intrbase+0x20300,
	AddrUart0 =	Intrbase+0x12000,
	AddrUart1 =	Intrbase+0x12100,
	AddrRtc =	Intrbase+0x10300,

	AddrDeviceid =	Intrbase+0x10034,
	AddrClockctl =	Intrbase+0x1004c,
	AddrIocfg0 =	Intrbase+0x100e0,
};

enum {
	IRQ0sum,
	IRQ0bridge,
	IRQ0h2cdoorbell,
	IRQ0c2hdoorbell,
	_IRQ0reserved0,
	IRQ0xor0chan0,
	IRQ0xor0chan1,
	IRQ0xor1chan0,
	IRQ0xor1chan1,
	IRQ0pex0int,
	_IRQ0reserved1,
	IRQ0gbe0sum,
	IRQ0gbe0rx,
	IRQ0gbe0tx,
	IRQ0gbe0misc,
	IRQ0gbe1sum,
	IRQ0gbe1rx,
	IRQ0gbe1tx,
	IRQ0gbe1misc,
	IRQ0usb0,
	_IRQ0reserved2,
	IRQ0sata,
	IRQ0crypto,
	IRQ0spi,
	IRQ0audio,
	_IRQ0reserved3,
	IRQ0ts0,
	_IRQ0reserved4,
	IRQ0sdio,
	IRQ0twsi,
	IRQ0avb,
	IRQ0tdm,
};

enum {
	_IRQ1reserved0,
	IRQ1uart0,
	IRQ1uart1,
	IRQ1gpiolo0,
	IRQ1gpiolo1,
	IRQ1gpiolo2,
	IRQ1gpiolo3,
	IRQ1gpiohi0,
	IRQ1gpiohi1,
	IRQ1gpiohi2,
	IRQ1gpiohi3,
	IRQ1xor0err,
	IRQ1xor1err,
	IRQ1pex0err,
	_IRQ1reserved1,
	IRQ1gbe0err,
	IRQ1gbe1err,
	IRQ1usberr,
	IRQ1cryptoerr,
	IRQ1audioerr,
	_IRQ1reserved2,
	_IRQ1reserved3,
	IRQ1rtc,
};

#define INTRREG		((IntrReg*)AddrIntr)
typedef struct IntrReg IntrReg;
struct IntrReg
{
	struct {
		ulong	irq;
		ulong	irqmask;
		ulong	fiqmask;
		ulong	epmask;
	} lo, hi;
};


#define TIMERREG	((TimerReg*)AddrTimer)
typedef struct TimerReg TimerReg;
struct TimerReg
{
	ulong	ctl;
	ulong	pad[3];
	ulong	reload0;
	ulong	timer0;
	ulong	reload1;
	ulong	timer1;
	ulong	watchreload;
	ulong	watchtimer;
};


enum {

	IRRintrmask	= (1<<4)-1,
	IRRnointr	= 1,
	IRRthrempty	= 2,
	IRRrxdata	= 4,
	IRRrxstatus	= 6,
	IRRtimeout	= 12,

	IRRfifomask	= 3<<6,
	IRRfifoenable	= 3<<6,


	FCRenable	= 1<<0,
	FCRrxreset	= 1<<1,
	FCRtxreset	= 1<<2,
	/* reserved */
	FCRrxtriggermask	= 3<<6,
	FCRrxtrigger1	= 0<<6,
	FCRrxtrigger4	= 1<<6,
	FCRrxtrigger8	= 2<<6,
	FCRrxtrigger14	= 3<<6,

	LCRbpcmask	= 3<<0,
	LCRbpc5		= 0<<0,
	LCRbpc6		= 1<<0,
	LCRbpc7		= 2<<0,
	LCRbpc8		= 3<<0,
	LCRstop2b	= 1<<2,
	LCRparity	= 1<<3,
	LCRparityeven	= 1<<4,
	LCRbreak	= 1<<6,
	LCRdivlatch	= 1<<7,

	LSRrx		= 1<<0,
	LSRrunerr	= 1<<1,
	LSRparerr	= 1<<2,
	LSRframeerr	= 1<<3,
	LSRbi		= 1<<4,
	LSRthre		= 1<<5,
	LSRtxempty	= 1<<6,
	LSRfifoerr	= 1<<7,
};

/* xxx should perhaps be done packed, with uchar's for the relevant fields? */
#define UART0REG	((UartReg*)AddrUart0)
#define UART1REG	((UartReg*)AddrUart1)
typedef struct UartReg UartReg;
struct UartReg
{
	union {
		ulong	thr;
		ulong	dll;
		ulong	rbr;
	};
	union {
		ulong	ier;
		ulong	dlh;
	};
	union {
		ulong	iir;
		ulong	fcr;
	};
	ulong	lcr;
	ulong	mcr;
	ulong	lsr;
	ulong	scr;
};

#define RTCREG	((RtcReg*)AddrRtc)
typedef struct RtcReg RtcReg;
struct RtcReg
{
	ulong	time;
	ulong	date;
	ulong	alarmtm;
	ulong	alarmdt;
	ulong	intrmask;
	ulong	intrcause;
};
