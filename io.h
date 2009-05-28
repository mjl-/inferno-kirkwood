enum {
	Regbase =	0xf1000000,
	AddrMpp =	Regbase+0x10000,
	AddrGpio0 =	Regbase+0x10100,
	AddrGpio1 =	Regbase+0x10140,
	AddrRtc =	Regbase+0x10300,

	AddrDeviceid =	Regbase+0x10034,
	AddrClockctl =	Regbase+0x1004c,
	AddrIocfg0 =	Regbase+0x100e0,
	AddrDevid =	Regbase+0x10034,

	AddrEfuse =	Regbase+0x1008c,

	AddrUart0 =	Regbase+0x12000,
	AddrUart1 =	Regbase+0x12100,

	AddrWin =	Regbase+0x20000,
	AddrCpucsr =	Regbase+0x20100,
	AddrIntr =	Regbase+0x20200,
	AddrTimer =	Regbase+0x20300,

	AddrTdmaAddr =	Regbase+0x30a00,
	AddrTdmaCtl =	Regbase+0x30800,
	AddrTdamIntr =	Regbase+0x308c8,

	AddrHash =	Regbase+0x3dd00,
	AddrDes =	Regbase+0x3dd40,
	AddrAesenc =	Regbase+0x3dd80,
	AddrAesdec =	Regbase+0x3ddc0,
	AddrCryptoIntr =Regbase+0x3de20,
	AddrSecurity =	Regbase+0x3de00,

	AddrGbe0 =	Regbase+0x72000,
	AddrGbe1 =	Regbase+0x76000,

	AddrSdio =	Regbase+0x90000,
};

enum {
	Irqlo, Irqhi, Irqbridge,
};

enum {
	IRQcpuself,
	IRQcputimer0,
	IRQcputimer1,
	IRQcputimerwd,
};


enum {
	RstoutPex =	1<<0,
	RstoutWatchdog =1<<1,
	RstoutSoft =	1<<2,

	ResetSystem =	1<<0,
};

#define CPUCSREG	((CpucsReg*)AddrCpucsr)
typedef struct CpucsReg CpucsReg;
struct CpucsReg
{
	ulong	cpucfg;
	ulong	cpucsr;
	ulong	rstout;
	ulong	softreset;
	ulong	irq;
	ulong	irqmask;
	ulong	mempm;
	ulong	clockgate;
	ulong	biu;
	ulong	pad0;
	ulong	l2cfg;
	ulong	pad1[2];
	ulong	l2tm0;
	ulong	l2tm1;
	ulong	pad2[2];
	ulong	l2pm;
	ulong	ram0;
	ulong	ram1;
	ulong	ram2;
	ulong	ram3;
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

	_IRQ1reserved0 = 0,
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


enum {
	Tmr0enable	= 1<<0,
	Tmr0periodic	= 1<<1,
	Tmr1enable	= 1<<2,
	Tmr1periodic	= 1<<3,
	TmrWDenable	= 1<<4,
	TmrWDperiodic	= 1<<5,
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
	ulong	reloadwd;
	ulong	timerwd;
};


enum {
	IERrx		= 1<<0,
	IERtx		= 1<<1,

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


#define AESDECREG	((AesReg*)AddrAesdec)
#define AESENCREG	((AesReg*)AddrAesenc)
typedef struct AesReg AesReg;
struct AesReg
{
	ulong	key[8];
	ulong	data[4];
	ulong	cmd;
};



#define GBE0REG	((GbeReg*)AddrGbe0)
#define GBE1REG	((GbeReg*)AddrGbe1)
#define PAD(next, last)	(((next)-4-(last))/4)
typedef struct GbeReg GbeReg;
struct GbeReg
{
	ulong	phy;
	ulong	smi;
	ulong	euda;
	ulong	eudid;
	ulong	pad0[PAD(0x080, 0x00c)];
	ulong	euic;
	ulong	euim;
	ulong	pad1[PAD(0x094, 0x084)];
	ulong	euea;
	ulong	euiae;
	ulong	pad2[PAD(0x0b0, 0x098)];
	ulong	euc;
	ulong	pad3[PAD(0x200, 0x0b0)];
	struct {
		ulong	addr;
		ulong	size;
	} base[6];
	ulong	pad4[PAD(0x280, 0x22c)];
	ulong	harr[4];
	ulong	bare;
	ulong	epap;

	ulong	pad5[PAD(0x400, 0x294)];
	ulong	portcfg;
	ulong	portcfgx;
	ulong	mii;
	ulong	pad6;
	ulong	evlane;
	ulong	macal;
	ulong	macah;
	ulong	sdc;
	ulong	dscp[7];
	ulong	psc0;
	ulong	vpt2p;
	ulong	ps0;
	ulong	tqc;
	ulong	psc1;
	ulong	ps1;
	ulong	mvhdr;
	ulong	pad8[2];
	ulong	irq;
	ulong	irqe;
	ulong	irqmask;
	ulong	irqemask;
	ulong	pad9;
	ulong	pxtfut;
	ulong	pad10;
	ulong	pxmfs;
	ulong	pad11;
	ulong	pxdfc;
	ulong	pxofc;
	ulong	pad12[2];
	ulong	piae;
	ulong	pad13[PAD(0x4bc, 0x494)];
	ulong	etherprio;
	ulong	pad14[PAD(0x4dc, 0x4bc)];
	ulong	tqfpc;
	ulong	pttbrc;
	ulong	tqc1;
	ulong	pmtu;
	ulong	pmtbs;
	ulong	pad15[PAD(0x600, 0x4ec)];
	struct {
		ulong	pad[3];
		ulong	r;
	} crdp[8];
	ulong	rqc;
	ulong	tcsdp;
	ulong	pad16[PAD(0x6c0, 0x684)];
	ulong	tcqdp[8];
	ulong	pad17[PAD(0x700, 0x6dc)];
	struct {
		ulong	tbctr;
		ulong	tbcfg;
		ulong	acfg;
		ulong	pad;
	} tq[8];
	ulong	pttbc;
	ulong	pad18[PAD(0x7a8, 0x780)];
	ulong	ipg2;
	ulong	pad19[3];
	ulong	ipg3;
	ulong	pad20;
	ulong	htlp;
	ulong	htap;
	ulong	ltap;
	ulong	pad21;
	ulong	ts;

	ulong	pad22[PAD(0x1400, 0x07d0)];
	ulong	dfsmt[64];
	ulong	dfomt[64];
	ulong	dfut[4];
};

#define SDIOREG	((SdioReg*)AddrSdio)
typedef struct SdioReg SdioReg;
struct SdioReg
{
	ulong	dmaaddrlo;
	ulong	dmaaddrhi;
	ulong	blksize;
	ulong	blkcount;
	ulong	argcmdlo;
	ulong	argcmdhi;
	ulong	txmode;
	ulong	cmd;
	ulong	resp[8];
	ulong	fifo;
	ulong	crc7rsp;
	ulong	hwstate;
	ulong	pad0;
	ulong	hostctl;
	ulong	blkgapctl;
	ulong	clkctl;
	ulong	swreset;
	ulong	norintrstat;
	ulong	errintrstat;
	ulong	norintrstatena;
	ulong	errintrstatena;
	ulong	norintrena;
	ulong	errintrena;
	ulong	acmd12errstat;
	ulong	currbyteleft;
	ulong	currblkleft;
	ulong	acmd12arglo;
	ulong	acmd12arghi;
	ulong	acmd12idx;
	ulong	acmdrsp[3];
	ulong	pad1[PAD(0x90100, 0x90098)];
	ulong	mbusctllo;
	ulong	mbusctlhi;
	struct {
		ulong	ctl;
		ulong	data;
	} win[4];
	ulong	clkdiv;
	ulong	addrdecerr;
	ulong	addrdecerrmsk;
};

#define MPPREG ((Reg*)AddrMpp)
typedef struct MppReg MppReg;
struct MppReg
{
	ulong	ctl[7];
	ulong	pad0[PAD(0x30, 0x18)];
	ulong	sampatrst;
};

#define GPIO0REG	((GpioReg*)AddrGpio0)
#define GPIO1REG	((GpioReg*)AddrGpio1)
typedef struct GpioReg GpioReg;
struct GpioReg
{
	ulong	dataout;
	ulong	dataoutena;
	ulong	blinkena;
	ulong	datainpol;
	ulong	datain;
	ulong	intrcause;
	ulong	intrmask;
	ulong	intrlevelmask;
};

#define	WINREG	((WinReg*)AddrWin)
typedef struct WinReg WinReg;
struct WinReg
{
	struct {
		ulong	ctl;
		ulong	base;
		ulong	remaplo;
		ulong	remaphi;
	} w[7];
	ulong	pad0[PAD(0x80, 0x74)];
	ulong	intbase;
};

#define EFUSEREG	((EfuseReg*)AddrEfuse)
typedef struct EfuseReg EfuseReg;
struct EfuseReg
{
	ulong	protection;
	ulong	pad0[PAD(0x100a4, 0x1008c)];
	ulong	lo0, hi0;
	ulong	lo1, hi1;
	ulong	ctl;
};
