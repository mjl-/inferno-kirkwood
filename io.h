#define PAD(start, end)	(((end)-4-(start))/4)

enum {
	Regbase		= 0xf1000000,
	AddrSDramc	= Regbase+0x01400,
	AddrSDramd	= Regbase+0x01500,

	AddrMpp		= Regbase+0x10000,
	AddrGpio0	= Regbase+0x10100,
	AddrGpio1	= Regbase+0x10140,
	AddrRtc		= Regbase+0x10300,
	AddrNandf       = Regbase+0x10418,

	AddrDeviceid	= Regbase+0x10034,
	AddrClockctl	= Regbase+0x1004c,
	AddrIocfg0	= Regbase+0x100e0,

	AddrEfuse	= Regbase+0x1008c,

	AddrTwsi	= Regbase+0x11000,

	AddrUart0	= Regbase+0x12000,
	AddrUart1	= Regbase+0x12100,

	AddrWin		= Regbase+0x20000,
	AddrCpucsr	= Regbase+0x20100,
	AddrIntr	= Regbase+0x20200,
	AddrTimer	= Regbase+0x20300,
	AddrL2win	= Regbase+0x20a00,

	AddrTdma	= Regbase+0x30800,
	AddrHash	= Regbase+0x3dd00,
	AddrDes		= Regbase+0x3dd40,
	AddrAesenc	= Regbase+0x3dd80,
	AddrAesdec	= Regbase+0x3ddc0,
	AddrCrypt	= Regbase+0x3de00,

	AddrUsb		= Regbase+0x50000,

	AddrXore0	= Regbase+0x60800,
	AddrXore1	= AddrXore0+0x100,
	AddrXore0p0	= Regbase+0x60810,
	AddrXore1p0	= AddrXore0p0+4,
	AddrXore0p1	= AddrXore0p0+0x100,
	AddrXore1p1	= AddrXore0p1+4,

	AddrGbe0	= Regbase+0x72000,
	AddrGbe1	= Regbase+0x76000,

	AddrSatahc	= Regbase+0x80000,
	AddrSata0	= Regbase+0x82000,
	AddrSata1	= Regbase+0x84000,
	AddrAta0	= Regbase+0x82100, /* sic, docs wrongly say 0xa2100 */
	AddrAta1	= Regbase+0x84100,

	AddrSdio	= Regbase+0x90000,

	AddrPhyNand	= 0xf9000000,
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
	RstoutPex	= 1<<0,
	RstoutWatchdog	= 1<<1,
	RstoutSoft	= 1<<2,

	ResetSystem	= 1<<0,

	/* CpucsReg.mempm, memory power down */
	Gbe0mem		= 1<<0,
	Pex0mem		= 1<<1,
	Usb0mem		= 1<<2,
	Dunitmem	= 1<<3,
	Runitmem	= 1<<4,
	Xor0mem		= 1<<5,
	Sata0mem	= 1<<6,
	Xor1mem		= 1<<7,
	Cryptomem	= 1<<8,
	Audiomem	= 1<<9,
	Sata1mem	= 1<<11,
	Gbe1mem		= 1<<13,

	/* CpucsReg.clockgate, clock enable */
	Gbe0clock	= 1<<0,
	Pex0clock	= 1<<2,
	Usb0clock	= 1<<3,
	Sdioclock	= 1<<4,
	Tsuclock	= 1<<5,
	Dunitclock	= 1<<6,
	Runitclock	= 1<<7,
	Xor0clock	= 1<<8,
	Audioclock	= 1<<9,
	Clockrealign	= 1<<10,
	Powersave	= 1<<11,
	Powerhalf	= 1<<12,
	Newclockratio	= 1<<13,
	Sata0clock	= 1<<14,
	Sata1clock	= 1<<15,
	Xor1clock	= 1<<16,
	Cryptoclock	= 1<<17,
	Gbe1clock	= 1<<19,
	Tdmclock	= 1<<20,

	/* CpucsReg.l2cfg, cpu l2 cfg */
	L2ecc		= 1<<2,
	L2exists	= 1<<3,
	L2wtmode	= 1<<4,

	Pexenable	= 1<<0,
	Intrblock	= 1<<28,
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

#define L2WINREG	((L2winReg*)AddrL2win);
typedef struct L2winReg L2winReg;
struct L2winReg
{
	struct {
		ulong	addr;
		ulong	size;
	} win[4];
};


enum {
	IERrx		= 1<<0,
	IERtx		= 1<<1,
	IERelsi		= 1<<2,
	IERems		= 1<<3,

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

	MCRrts		= 1<<1,
	MCRloop		= 1<<4,

	MSRdcts		= 1<<0,
	MSRctx		= 1<<4,
	
	LSRrx		= 1<<0,
	LSRrunerr	= 1<<1,
	LSRparerr	= 1<<2,
	LSRframeerr	= 1<<3,
	LSRbi		= 1<<4,
	LSRthre		= 1<<5,
	LSRtxempty	= 1<<6,
	LSRfifoerr	= 1<<7,
};

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
	ulong	msr;
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

#define NANDFREG ((NandfReg*)AddrNandf)
typedef struct NandfReg NandfReg;
struct NandfReg
{
	ulong	rdparms;
	ulong	wrparms;
	uchar	_pad0[0x70-0x20];
	ulong	ctl;
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

#define DESREG	((DesReg*)AddrDes)
typedef struct DesReg DesReg;
struct DesReg
{
	ulong	ivlo;
	ulong	ivhi;
	ulong	key0lo;
	ulong	key0hi;
	ulong	key1lo;
	ulong	key1hi;
	ulong	cmd;
	ulong	pad0;
	ulong	key2lo;
	ulong	key2hi;
	ulong	pad1[2];
	ulong	datalo;
	ulong	datahi;
	ulong	outlo;
	ulong	outhi;
};

#define HASHREG	((HashReg*)AddrHash)
typedef struct HashReg HashReg;
struct HashReg
{
	ulong	iv[5];
	ulong	pad0;
	ulong	cmd;
	ulong	pad1;
	ulong	bitcountlo;
	ulong	bitcounthi;	/* 0x3dd24 */
	ulong	pad2[PAD(0x3dd24, 0x3dd38)];
	ulong	data;		/* 0x3dd38 */
};

#define CRYPTREG	((CryptReg*)AddrCrypt)
typedef struct CryptReg CryptReg;
struct CryptReg
{
	ulong	cmd;
	ulong	descr;
	ulong	cfg;
	ulong	status;	/* 0x3de0c */
	ulong	pad0[PAD(0x3de0c, 0x3de20)];
	ulong	irq;	/* 0x3de20 */
	ulong	irqmask;
};

#define TDMAREG	((TdmaReg*)AddrTdma)
typedef struct TdmaReg TdmaReg;
struct TdmaReg
{
	ulong	bytecount;
	ulong	pad0[3];
	ulong	src;
	ulong	pad1[3];
	ulong	dst;
	ulong	pad2[3];
	ulong	nextdescr;
	ulong	pad3[3];
	ulong	ctl;
	ulong	pad4[3];
	ulong	curdescr;	/* 0x30870 */
	ulong	pad5[PAD(0x30870, 0x308c8)];
	ulong	irqerr;		/* 0x308c8 */
	ulong	irqerrmask;	/* 0x308cc */
	ulong	pad6[PAD(0x308cc, 0x30a00)];
	struct	{
		ulong	bar;
		ulong	winctl;
	} addr[4];
};


#define GBE0REG	((GbeReg*)AddrGbe0)
#define GBE1REG	((GbeReg*)AddrGbe1)
typedef struct GbeReg GbeReg;
struct GbeReg
{
	ulong	phy;
	ulong	smi;
	ulong	euda;
	ulong	eudid;
	ulong	_pad0[PAD(0x7200c, 0x72080)];
	ulong	euirq;
	ulong	euirqmask;
	ulong	_pad1[PAD(0x72084, 0x72094)];
	ulong	euea;
	ulong	euiae;
	ulong	_pad2[PAD(0x72098, 0x720b0)];
	ulong	euc;
	ulong	_pad3[PAD(0x720b0, 0x72200)];
	struct {
		ulong	base;
		ulong	size;
	} base[6];
	ulong	_pad4[PAD(0x7222c, 0x72280)];
	ulong	harr[4];
	ulong	bare;
	ulong	epap;

	ulong	_pad5[PAD(0x72294, 0x72400)];
	ulong	portcfg;
	ulong	portcfgx;
	ulong	mii;
	ulong	_pad6;
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
	ulong	_pad8[2];
	ulong	irq;
	ulong	irqe;
	ulong	irqmask;
	ulong	irqemask;
	ulong	_pad9;
	ulong	pxtfut;
	ulong	_pad10;
	ulong	pxmfs;
	ulong	_pad11;
	ulong	pxdfc;
	ulong	pxofc;
	ulong	_pad12[2];
	ulong	piae;
	ulong	_pad13[PAD(0x72494, 0x724bc)];
	ulong	etherprio;
	ulong	_pad14[PAD(0x724bc, 0x724dc)];
	ulong	tqfpc;
	ulong	pttbrc;
	ulong	tqc1;
	ulong	pmtu;
	ulong	pmtbs;
	ulong	_pad15[PAD(0x724ec, 0x72600)];
	struct {
		ulong	_pad[3];
		ulong	r;
	} crdp[8];
	ulong	rqc;
	ulong	tcsdp;
	ulong	_pad16[PAD(0x72684, 0x726c0)];
	ulong	tcqdp[8];
	ulong	_pad17[PAD(0x726dc, 0x72700)];
	struct {
		ulong	tbctr;
		ulong	tbcfg;
		ulong	acfg;
		ulong	_pad;
	} tq[8];
	ulong	pttbc;
	ulong	_pad18[PAD(0x72780, 0x727a8)];
	ulong	ipg2;
	ulong	_pad19[3];
	ulong	ipg3;
	ulong	_pad20;
	ulong	htlp;
	ulong	htap;
	ulong	ltap;
	ulong	_pad21;
	ulong	ts;		/* 0x727d0 */

	ulong	_pad22[PAD(0x727d0, 0x73000)];
	ulong	rxoctetshi;
	ulong	rxoctetslo;
	ulong	badrxoctets;
	ulong	mactxerror;
	ulong	rxframes;
	ulong	badrxframes;
	ulong	rxbroadcastframes;
	ulong	rxmulticastframes;
	ulong	rxframe64;
	ulong	rxframe65to127;
	ulong	rxframe128to255;
	ulong	rxframe256to511;
	ulong	rxframe512to1023;
	ulong	rxframe1024tomax;
	ulong	txoctetshi;
	ulong	txoctetslo;
	ulong	txframes;
	ulong	txcollisionframedrop;
	ulong	txmulticastframes;
	ulong	txbroadcastframes;
	ulong	badmaccontrolframes;
	ulong	txflowcontrol;
	ulong	rxflowcontrol;
	ulong	badrxflowcontrol;
	ulong	rxundersized;
	ulong	rxfragments;
	ulong	rxoversized;
	ulong	rxjabber;
	ulong	rxerrors;
	ulong	crcerrors;
	ulong	collisions;
	ulong	latecollisions;		/* 0x7307c */

	ulong	_pad23[PAD(0x7307c, 0x73400)];
	ulong	dfsmt[64];	/* 0x73400 */
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
	ulong	arglo;
	ulong	arghi;
	ulong	mode;
	ulong	cmd;			/* cmds, write starts transaction */
	ulong	resp[8];
	ulong	fifo;
	ulong	crc7;
	ulong	hoststate;
	ulong	pad0;
	ulong	hostctl;
	ulong	blkgapctl;
	ulong	clkctl;
	ulong	swreset;
	ulong	st;			/* status */
	ulong	est;			/* error status */
	ulong	stena;			/* status enable */
	ulong	estena;			/* error status enable */
	ulong	stirq;			/* status irq enable */
	ulong	estirq;			/* error status irq enable */
	ulong	acmd12st;		/* auto cmd 12 status */
	ulong	currbyteleft;
	ulong	currblkleft;
	ulong	acmd12arglo;
	ulong	acmd12arghi;
	ulong	acmd12idx;
	ulong	acmdrsp[3];
	ulong	pad1[PAD(0x90098, 0x90100)];
	ulong	mbusctllo;
	ulong	mbusctlhi;
	struct {
		ulong	ctl;
		ulong	data;
	} win[4];
	ulong	clockdiv;
	ulong	addrdecerr;
	ulong	addrdecerrmsk;
};

#define MPPREG ((MppReg*)AddrMpp)
typedef struct MppReg MppReg;
struct MppReg
{
	ulong	ctl[7];
	ulong	pad0[PAD(0x18, 0x30)];
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
	ulong	intredgeena;
	ulong	intrlevelena;
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
	ulong	pad0[PAD(0x74, 0x80)];
	ulong	intbase;
};

#define EFUSEREG	((EfuseReg*)AddrEfuse)
typedef struct EfuseReg EfuseReg;
struct EfuseReg
{
	ulong	protection;
	ulong	pad0[PAD(0x1008c, 0x100a4)];
	ulong	lo0, hi0;
	ulong	lo1, hi1;
	ulong	ctl;
};

#define XORE0REG	((XoreReg*)AddrXore0)
#define XORE1REG	((XoreReg*)AddrXore1)
typedef struct XoreReg XoreReg;
struct XoreReg
{
	ulong	xechar;		/* 0x60800 */
	ulong	pad0[PAD(0x60800, 0x60830)];
	ulong	irq;		/* 0x60830 */
	ulong	pad1[3];
	ulong	irqmask;	/* 0x60840 */
	ulong	pad2[3];
	ulong	errorcause;	/* 0x60850 */
	ulong	pad3[3];
	ulong	erroraddr;	/* 0x60860 */
	ulong	pad4[PAD(0x60860, 0x60a50)];
	ulong	bar[8];		/* 0x60a50 */
	ulong	sizemask[8];	/* 0x60a70 */
	ulong	harr[4];	/* 0x60a90 */
	ulong	pad7[PAD(0x60a90+3*4, 0x60ae0)];
	ulong	initvallo;	/* 0x60ae0 */
	ulong	initvalhi;	/* 0x60ae4 */
};

#define XORE0P0REG	((XorReg*)AddrXore0p0)
#define XORE1P0REG	((XorReg*)AddrXore1p0)
#define XORE0P1REG	((XorReg*)AddrXore0p1)
#define XORE1P1REG	((XorReg*)AddrXore1p1)
typedef struct XorReg XorReg;
struct XorReg
{
	ulong	cfg;		/* 0x60810 */
	ulong	pad0[3];
	ulong	act;		/* 0x60820 */
	ulong	pad1[PAD(0x60820, 0x60a00)];
	ulong	nextdescr;	/* 0x60a00 */
	ulong	pad2[3];
	ulong	curdescr;	/* 0x60a10 */
	ulong	pad3[3];
	ulong	bytecount;	/* 0x60a20 */
	ulong	pad4[PAD(0x60a20, 0x60a40)];
	ulong	winctl;		/* 0x60a40 */
	ulong	pad5[PAD(0x60a40, 0x60aa0)];
	ulong	aoctl;		/* 0x60aa0 */
	ulong	pad6[3];
	ulong	dest;		/* 0x60ab0 */
	ulong	pad7[3];
	ulong	blocksize;	/* 0x60ac0 */
};

#define SDRAMCREG	((SDramcReg*)AddrSDramc)
typedef struct SDramcReg SDramcReg;
struct SDramcReg
{
	ulong	ctl;
	ulong	ddrctllo;
	ulong	timinglo;
	ulong	timinghi;
	ulong	addrctl;
	ulong	opagectl;
	ulong	oper;
	ulong	mode;
	ulong	extmode;
	ulong	ddrctlhi;
	ulong	ddr2timelo;
	ulong	operctl;
	ulong	mbusctllo;
	ulong	mbusctlhi;
	ulong	mbustimeout;
	uchar	pad0[0x7c-0x3c];
	ulong	ddrtimehi;
	ulong	sdinitctl;
	ulong	pad1[2];
	ulong	extsdmode1;
	ulong	extsdmode2;
	ulong	odtctllo;
	ulong	odtctlhi;
	ulong	ddrodtctl;
	ulong	rbuffsel;

	uchar	pad2[0xc0-0xa8];
	ulong	accalib;
	ulong	dqcalib;
	ulong	dqscalib;
};

#define SDRAMDREG	((SDramdReg*)AddrSDramd)
typedef struct SDramdReg SDramdReg;
struct SDramdReg
{
	struct	{
		ulong	base;
		ulong	size;
	} win[4];
};

#define USBREG	((UsbReg*)AddrUsb)
typedef struct UsbReg UsbReg;
struct UsbReg
{
	/* USB 2.0 HS OTG core ref (see FS pag. 631) */
	ulong	id;
	ulong	hwgeneral;
	ulong	hwhost;
	ulong	hwdevice;
	ulong	hwtxbuf;
	ulong	hwrxbuf;
	ulong	hwtttxbuf;
	ulong	hwttrxbuf;
	ulong   pad0[PAD(0x5001c, 0x50100)];
	uchar	caplength;
	uchar	pad1[1];
	ushort	hciversion;
	ulong	hcsparams;
	ulong	hccparams;
	ulong   pad2[PAD(0x50108, 0x50120)];
	ushort	dciversion;
	ushort	pad3[1];
	ulong	dccparams;	
	ulong   pad4[PAD(0x50124, 0x50140)];
	ulong	usbcmd;
	ulong	usbsts;
	ulong	usbintr;
	ulong	frindex;
	ulong	pad5[PAD(0x5014c, 0x50154)];
	ulong	devaddr;
	ulong	endptaddr;
	ulong	ttctrl;
	ulong	burstsz;
	ulong	txfilltun;
	ulong	txttfilltun;
	ulong	pad6[PAD(0x50168, 0x50180)];
	ulong	configflag;
	ulong	portsc1;
	ulong	pad7[PAD(0x50184, 0x501a4)];
	ulong	otgsc;
	ulong	usbmode;
	struct {
		ulong	setupstat;
		ulong	prime;
		ulong	flush;
		ulong	status;
		ulong	complete;
		ulong	ctrl[4];
	} endpt;

	/* USB 2.0 see FS pag. 625 */
	ulong   pad7[PAD(0x501cc, 0x50300)];
	ulong	brdgctl;
	ulong   pad8[PAD(0x50300, 0x50310)];
	struct {
		ulong	intr;
		ulong 	intrmask;
		ulong	pad0;
		ulong	erraddr;
	} brdg;
	struct	{
		ulong	ctl;
		ulong	base;
		ulong	pad0[2];
	} win[4];
	ulong	phyconf;
	ulong   pad9[PAD(0x50360, 0x50400)];
	ulong	power;
};


#define SATAHCREG ((SatahcReg*)AddrSatahc)
typedef struct SatahcReg SatahcReg;
struct SatahcReg
{
	ulong	cfg;
	ulong	qout;
	ulong	qin;
	ulong	intrcoalesc;
	ulong	intrtime;
	ulong	intr;
	ulong	_pad0[2];
	ulong	intrmain;
	ulong	intrmainmask;
	ulong	_pad1;
	ulong	ledcfg;
	struct	{
		ulong	ctl;
		ulong	base;
		ulong	_pad0[2];
	} win[4];
};

#define SATA0REG ((SataReg*)AddrSata0)
#define SATA1REG ((SataReg*)AddrSata1)
typedef struct SataReg SataReg;
struct SataReg
{
	/* edma */
	ulong	cfg;
	ulong	_pad0;
	ulong	intre;
	ulong	intremask;
	ulong	reqbasehi;
	ulong	reqin;
	ulong	reqout;
	ulong	respbasehi;
	ulong	respin;
	ulong	respout;
	ulong	cmd;
	ulong	_pad1;
	ulong	status;
	ulong	iordytimeout;
	ulong	_pad2[2];
	ulong	cmddelaythr;
	uchar	_pad3[0x50-0x44];

	/* sata interface */
	ulong	ifccfg;
	ulong	pllcfg;
	uchar	_pad4[0x60-0x58];

	/* edma */
	ulong	haltcond;
	uchar	_pad5[0x94-0x64];
	ulong	ncqdone;
	uchar	_pad6[0x224-0x98];

	/* basic dma */
	struct {
		ulong	cmd;
		ulong	status;
		ulong	dtlo;
		ulong	dthi;
		ulong	drlo;
		ulong	drhi;
	} bdma;
	uchar	_pad7[0x300-0x23c];

	/* sata interface */
	ulong	sstatus;
	ulong	serror;
	ulong	scontrol;
	ulong	ltmode;
	ulong	phym3;
	ulong	phym4;
	ulong	_pad8[5];
	ulong	phym1;
	ulong	phym2;
	ulong	bistctl;
	ulong	bist1;
	ulong	bist2;
	ulong	serrintrmask;
	ulong	ifcctl;
	ulong	ifctestctl;
	ulong	ifcstatus;
	ulong	_pad9[3];
	ulong	vendor;
	ulong	fiscfg;
	ulong	fisintr;
	ulong	fisintrmask;
	ulong	_pad10;
	ulong	fis[7];
	ulong	_pad11[3];
	ulong	phym9g2;
	ulong	phym9g1;
	ulong	phycfg;
	ulong	phytctl;
	ulong	phym10;
	ulong	_pad12;
	ulong	phym12;
};

#define ATA0REG	((AtaReg*)AddrAta0)
#define ATA1REG	((AtaReg*)AddrAta1)
typedef struct AtaReg AtaReg;
struct AtaReg
{
	ulong	data;
	union {
		ulong	feat;
		ulong	error;
	};
	ulong	sectors;
	ulong	lbalow;
	ulong	lbamid;
	ulong	lbahigh;
	ulong	dev;
	union {
		ulong	cmd;
		ulong	status;
	};
	ulong	ctl;
};
