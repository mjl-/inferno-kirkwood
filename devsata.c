/*
first attempt at driver for the sata controller.
kirkwood has two ports, on the sheevaplug only the second port is in use.
for now we'll assume ncq "first party dma" read/write commands (very old sata disks won't work).

todo:
- when doing ata commands, verify ata status if ok.
- get interrupt when device sends registers.  so we can check for BSY then, or start reading data, etc.
- error handling & propagating to caller.
- better detect ata support of drives
- detect whether packet command is accepted.  try if packet commands work.
- read ata/atapi signature in registers after reset?
- look at cache flushes around dma
- hotplug, at least handle disconnects
- interrupt coalescing
- general ata commands
- support: pio,dma,ncq with ata commands.  perhaps atapi-mmc & atapi-scsi too?  this chip only does pio with atapi...
- use generic sd interface (#S;  we need partitions)
- abstract for multiple ports (add controller struct to functions)
- in satainit(), only start the disk init, don't wait for it to be ready?  faster booting, seems it takes controller/disk some time to init after reset.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"io.h"

static int satadebug = 1;
#define diprint	if(satadebug)iprint
#define dprint	if(satadebug)print

char Enodisk[] = "no disk";
char Etimeout[] = "timeout";

typedef struct Req Req;
typedef struct Resp Resp;
typedef struct Prd Prd;

/* edma request, set by us for reads & writes */
struct Req
{
	ulong	prdlo;	/* direct buffer when <= 64k, pointer to Prd for data up to 512k */
	ulong	prdhi;
	ulong	ctl;
	ulong	count;
	ulong	ata[4];
};
enum {
	/* Req.ctl */
	Rdev2mem	= 1<<0,
	Rdevtagshift	= 1,
	Rpmportshift	= 12,
	Rprdsingle	= 1<<16,
	Rhosttagshift	= 17,
};

/* edma response, set by edma with result of request */
struct Resp
{
	ulong	idflags;	/* 16 bit id, 16 bit flags */
	ulong	ts;		/* timestamp, reserved */
};

/* vectored i/o, only used for >64k requests */
struct Prd
{
	ulong	addrlo;		/* address of buffer, must be 2-byte aligned */
	ulong	flagcount;	/* low 16 bit is cound (0 means 64k), high 16 bit flags. */
	ulong	addrhi;		/* must be 0 */
	ulong	pad;
};
enum {
	/* Prd.flagcount */
	Endoftable	= 1<<31,
};

typedef struct Disk Disk;
struct Disk
{
	int	valid;
	char	serial[20+1];
	char	firmware[8+1];
	char	model[40+1];
	uvlong	sectors;
};

/*
 * with ncq we get 32 tags.  the controller has 32 slots, to write
 * commands to the device.  if a tag is available, there is also always
 * a slot available.  so we administer by tag.
 */
static uchar tags[32];
static int tagnext;
static int tagsinuse;
static Rendez tagl;	/* protected by reqsl */

static Rendez reqsr[32];	/* protected by requiring to hold tag */
static volatile ulong reqsdone[32];
static QLock reqsl;

static Req *reqs;
static Resp *resps;
static Prd *prds;
static int reqnext;
static int respnext;
static Disk disk;

enum {
	/* edma config */
	ECFGncq		= 1<<5,		/* use ncq */
	ECFGqueue	= 1<<9,		/* use ata queue dma commands */

	/* edma interrup error cause */
	Edeverr		= 1<<2,		/* device error */
	Edevdis		= 1<<3,		/* device disconnect */
	Edevcon		= 1<<4,		/* device connected */
	Eserror		= 1<<5,		/* serror set */
	Eselfdis	= 1<<7,		/* edma self disabled */
	Etransint	= 1<<8,		/* edma transport layer interrupt */
	Eiordy		= 1<<12,	/* edma iordy timeout */

	Elinkmask	= 0xf,		/* link rx/tx control/data errors: */
	Erxctlshift	= 13,
	Erxdatashift	= 17,
	Etxctlshift	= 21,
	Etxdatashift	= 26,
	Esatacrc	= 1<<0,		/* sata crc error */
	Efifo		= 1<<1,		/* internal fifo error */
	Eresetsync	= 1<<2,		/* link layer reset by SYNC from device */
	Eotherrxctl	= 1<<3,		/* other link ctl errors */

	/* edma command */
	EdmaEnable	= 1<<0,		/* enable edma */
	EdmaAbort	= 1<<1,		/* abort and disable edma */
	Atareset	= 1<<2,		/* reset sata transport, link, physical layers */
	EdmaFreeze	= 1<<4,		/* do not process new requests from queue */

	/* edma status */
	Tagmask		= (1<<5)-1,	/* of last commands */
	Dev2mem		= 1<<5,		/* direction of command */
	Cacheempty	= 1<<6,		/* edma cache empty */
	EdmaIdle	= 1<<7,		/* edma idle (but disk can have command pending) */

	/* host controller interrupt */
	Sata0err	= 1<<0,
	Sata0done	= 1<<1,
	Sata1err	= 1<<2,
	Sata1done	= 1<<3,
	Sata0dmadone	= 1<<4,
	Sata1dmadone	= 1<<5,
	Satacoaldone	= 1<<8,

	/* interface cfg */
	SSC		= 1<<6,		/* SSC enable */
	Gen2		= 1<<7,		/* gen2 enable */
	Comm		= 1<<8,		/* phy communication enable, override "DET" in SControl */
	Physhutdown	= 1<<9,		/* phy shutdown */
	Emphadj		= 1<<13,	/* emphasis level adjust */
	Emphtx		= 1<<14,	/* tx emphasis enable */
	Emphpre		= 1<<15,	/* pre-emphasis */
	Ignorebsy	= 1<<24,	/* ignore bsy in ata register */
	Linkrstenable	= 1<<25,

	/* SStatus */
	SDETmask	= 0xf,
	SDETnone	= 0,		/* no dev, no phy comm */
	SDETdev		= 1,		/* only dev present, no phy comm */
	SDETdevphy	= 3,		/* dev & phy comm */
	SDETnophy	= 4,		/* phy in offline mode (disabled or loopback) */
	SSPDmask	= 0xf<<4,
	SSPDgen1	= 1<<4,		/* gen 1 speed */
	SSPDgen2	= 2<<4,		/* gen 2 speed */

	/* SError */
	EM		= 1<<1,		/* recovered communication error */
	EN		= 1<<16,	/* phy ready state changed */
	EW		= 1<<18,	/* comm wake detected by phy */
	EB		= 1<<19,	/* 10 to 8 bit decode error */
	ED		= 1<<20,	/* incorrect disparity */
	EC		= 1<<21,	/* crc error */
	EH		= 1<<22,	/* handshake error */
	ES		= 1<<23,	/* link sequence error */
	ET		= 1<<24,	/* transport state transition error */
	EX		= 1<<26,	/* device presence changed */
	
	/* SControl */
	CDETmask	= 0xf<<0,
	CDETnone	= 0<<0,		/* no device detection/initialisation */
	CDETcomm	= 1<<0,		/* perform interface communication initialisation */
	CDETdisphy	= 4<<0,		/* disable sata interface, phy offline */
	CSPDmask	= 0xf<<4,
	CSPDany		= 0<<4,		/* no speed limitation */
	CSPDgen1	= 1<<4,		/* <= gen1 */
	CSPDgen2	= 2<<4,		/* <= gen2 */
	CIPMmask	= 0xf<<8,
	CIPMany		= 0<<8,		/* no interface power management state restrictions */
	CIPMnopartial	= 1<<8,		/* no transition to PARTIAL */
	CIPMnoslumber	= 1<<9,		/* no transition to SLUMBER */
	CSPMmask	= 0xf<<12,
	CSPMnone	= 0<<12,	/* no different state for select power management */
	CSPMpartial	= 1<<12,	/* to PARTIAL */
	CSPMslumber	= 2<<12,	/* to SLUMBER */
	CSPMactive	= 3<<12,	/* to active */

	/* ata status */
	Aerr		= 1<<0,
	Adrq		= 1<<3,
	Adf		= 1<<5,
	Adrdy		= 1<<6,
	Absy		= 1<<7,
};


enum {
	Qdir, Qctlr, Qctl, Qdata, Qtest,
};
static Dirtab satadir[] = {
	".",		{Qdir,0,QTDIR},	0,	0555,
	"sd01",		{Qctlr,0,QTDIR}, 0,	0555,
	"ctl",		{Qctl,0,0},	0,	0660,
	"data",		{Qdata,0,0},	0,	0660,
	"test",		{Qtest,0,0},	0,	0666,	 /* to be removed */
};


/* for reading numbers in response of "identify device" */
static ulong
g16(uchar *p)
{
	return ((ulong)p[0]<<8) | ((ulong)p[1]<<0);
}

static void
sataintr(Ureg*, void*)
{
	SatahcReg *hr = SATAHCREG;
	SataReg *sr = SATA1REG;
	ulong v, tag;
	static int count = 0;
	ulong in, out;
	
	v = hr->intrmain;
	diprint("intr %#lux, main %#lux\n", hr->intr, v);
	if(v & Sata1err) {
		diprint("intre %#lux\n", sr->edma.intre);
		diprint("m 1err\n");
	}
	if(v & Sata1done) {
		diprint("m 1done\n");
	}
	hr->intr = 0;
	sr->edma.intre = 0;
	intrclear(Irqlo, IRQ0sata);

	diprint("reqin %#lux reqout %#lux respin %#lux respout %#lux\n", sr->edma.reqin, sr->edma.reqout, sr->edma.respin, sr->edma.respout);

	dcinv(resps, 32*sizeof resps[0]);
	in = (sr->edma.respin & MASK(8))/sizeof (Resp);
	out = (sr->edma.respout & MASK(8))/sizeof (Resp);
	for(;;) {
		if(in == out)
			break;

		/* determine which request is done, wakeup its caller. */
		tag = resps[out].idflags & MASK(5);

		/* xxx check for error? */

		reqsdone[tag] = 1;
		wakeup(&reqsr[tag]);
		out = (out+1)%32;
		sr->edma.respout = (ulong)&resps[out];
	}
}

static void
pioget(uchar *p)
{
	AtaReg *a = ATA1REG;
	ulong v;
	int i;

	for(i = 0; i < 256; i++) {
		v = a->data;
		*p++ = v>>8;
		*p++ = v>>0;
	}
}

static void
pioput(uchar *p)
{
	AtaReg *a = ATA1REG;
	ulong v;
	int i;

	for(i = 0; i < 256; i++) {
		v = (ulong)*p++<<8;
		v |= (ulong)*p++<<0;
		a->data = v;
	}
}

enum {
	Nodata, Host2dev, Dev2host,
};
static ulong
atacmd(uchar cmd, uchar feat, uchar sectors, ulong lba, uchar dev, int dir, uchar *data)
{
	SatahcReg *hr = SATAHCREG;
	SataReg *sr = SATA1REG;
	AtaReg *a = ATA1REG;
	ulong v;

	/* xxx sleep until edma is disabled or edma is idle (edma status, bit 7 (EDMAIdle).  then disable edma. */

	/* xxx don't blindly reset registers later */
	hr->intrmainena &= ~(Sata1err|Sata1done|Sata1dmadone);

	diprint("ata, status %#lux\n", a->status);
	a->feat = feat;
	a->sectors = sectors;
	a->lbalow = (lba>>0) & 0xff;
	a->lbamid = (lba>>8) & 0xff;
	a->lbahigh = (lba>>16) & 0xff;
	a->dev = dev;
	a->cmd = cmd;
	delay(100);
	v = a->status;
if(satadebug) {
	diprint("result, status %#lux\n", v);
	if(v & Aerr)	diprint("  err\n");
	if(v & Adrq)	diprint("  drq\n");
	if(v & Adf)	diprint("  df\n");
	if(v & Adrdy)	diprint("  drdy\n");
	if(v & Absy)	diprint("  bsy\n");
}
	/* xxx check for & propagate errors */

	switch(dir) {
	case Nodata:
		break;
	case Host2dev:
		pioput(data);
		break;
	case Dev2host:
		pioget(data);
		break;
	}

	hr->intrmainena |= Sata1err|Sata1done|Sata1dmadone;
	hr->intr = 0;
	return v;
}

/* strip spaces in string.  at least western digital returns space-padded strings for "identify device". */
static void
strip(char *p)
{
	int i, j;

	for(i = 0; p[i] == ' '; i++)
		{}
	for(j = strlen(p)-1; j >= i && p[j] == ' '; j--)
		{}
	memmove(p, p+i, j+1-i);
	p[j+1-i] = 0;
}


/* output of "identify device", 256 16-bit words. */
enum {
	/* some words are only valid when bit 15 is 0 and bit 14 is 1. */
	Fvalidmask		= 3<<14,
	Fvalid			= 1<<14,

	/* capabilities.  these should be 1 for sata devices. */
	Fcaplba			= 1<<9,
	Fcapdma			= 1<<8,

	/* ata commands supported (word 82), enabled (word 85) */
	Feat0ServiceIntr	= 1<<8,
	Feat0Writecache		= 1<<5,
	Feat0Packet		= 1<<4,
	Feat0PowerMgmt		= 1<<3,
	Feat0SMART		= 1<<0,

	/* ata commands supported (word 83), enabled (word 86) */
	Feat1FlushCachExt	= 1<<13,
	Feat1FlushCache		= 1<<12,
	Feat1Addr48		= 1<<10,
	Feat1AAM		= 1<<9,		/* automatic acoustic management */
	Feat1SetFeatReq		= 1<<6,
	Feat1PowerupStandby	= 1<<5,
	Feat1AdvPowerMgmt	= 1<<3,

	/* ata commands supported (word 84), enabled (word 87) */
	Feat2WWName64		= 1<<8,
	Feat2Logging		= 1<<5,
	Feat2SMARTselftest	= 1<<1,
	Feat2SMARTerrorlog	= 1<<0,

	/* logical/physical sectors, word 106 */
	MultiLogicalSectors	= 1<<13,	/* multiple logical sectors per physical sector */
	LargeLogicalSectors	= 1<<12,	/* logical sector larger than 512 bytes */
	LogicalPerPhyslog2mask	= (1<<4)-1,

	/* sata capabilities, word 76 */
	SataCapNCQ		= 1<<8,
	SataCapGen2		= 1<<2,
	SataCapGen1		= 1<<1,

	/* nvcache capabilities, word 214 */
	NvcacheEnabled		= 1<<4,
	NvcachePMEnabled	= 1<<1,
	NvcachePMSup		= 1<<0,
};


typedef struct Atadev Atadev;
struct Atadev {
	ushort	major;
	ushort	minor;
	ushort	cmdset[6];	/* words 83-87.  first three are for "supported", last three for "enabled". */
	ushort	sectorflags;	/* word 106 */
	uvlong	wwn;		/* world wide name, unique.  0 if not supported. */
	ushort	sectorsize;	/* logical sector size */
	ushort	satacap;
	ushort	nvcachecap;
	ulong	nvcachelblocks;	/* in logical blocks */
	ushort	rpm;		/* 0 for unknown, 1 for non-rotating device, other for rpm */
};


static int
identify(void)
{
	uchar c;
	uchar buf[512];
	int i;
	ushort w;
	Atadev dev;

	atacmd(0xec, 0, 0, 0, 0, Dev2host, buf);

	c = 0;
	for(i = 0; i < 512; i++)
		c += buf[i];
	if(c != 0) {
		dprint("check byte for 'identify device' response invalid\n");
		return -1;
	}

	memmove(disk.serial, buf+10*2, sizeof disk.serial-1);
	memmove(disk.firmware, buf+23*2, sizeof disk.firmware-1);
	memmove(disk.model, buf+27*2, sizeof disk.model-1);
	strip(disk.serial);
	strip(disk.firmware);
	strip(disk.model);
	disk.sectors = 0;
	disk.sectors |= (uvlong)g16(buf+100*2)<<0;
	disk.sectors |= (uvlong)g16(buf+101*2)<<16;
	disk.sectors |= (uvlong)g16(buf+102*2)<<32;

	w = g16(buf+49*2);
	if((w & Fcapdma) == 0 || (w & Fcaplba) == 0) {
		dprint("disk does not support dma and/or lba\n");
		return -1;
	}

	dev.major = g16(buf+80*2);
	dev.minor = g16(buf+81*2);
	for(i = 0; i < 6; i++) {
		dev.cmdset[i] = g16(buf+(82+i)*2);
	}
	if((dev.cmdset[1] & Fvalidmask) != Fvalid)
		dev.cmdset[1] = 0;
	if((dev.cmdset[2] & Fvalidmask) != Fvalid)
		dev.cmdset[2] = 0;
	if((dev.cmdset[3+2] & Fvalidmask) != Fvalid)
		dev.cmdset[3+2] = 0;

	if((dev.cmdset[3+1] & Feat1Addr48) == 0) {
		dprint("disk does not have lba48 enabled\n");
		return -1;
	}

	dev.sectorflags = g16(buf+106*2);
	dev.sectorsize = 512;
	if((dev.sectorflags & Fvalidmask) != Fvalid)
		dev.sectorflags = 0;
	if(dev.sectorflags & LargeLogicalSectors)
		dev.sectorsize = 2 * (g16(buf+117*2)<<16 | g16(buf+118*2)<<0);

	dev.wwn = 0;
	if((dev.cmdset[2] & Feat2WWName64) && (dev.cmdset[3+2] & Feat2WWName64))
		dev.wwn = (uvlong)g16(buf+108*2)<<48 | (uvlong)g16(buf+109*2)<<32 | (uvlong)g16(buf+110*2)<<16 | (uvlong)g16(buf+111*2)<<0;

	dev.satacap = g16(buf+76*2);
	if(dev.satacap == 0xffff)
		dev.satacap = 0;

	dev.nvcachecap = g16(buf+214*2);
	dev.nvcachelblocks = g16(buf+215*2)<<16 | g16(buf+216*2)<<0;

	dev.rpm = g16(buf+217*2);
	/* check for "reserved" range in ata8-acs, set to 0 unknown if so */
	if(dev.rpm > 1 && dev.rpm < 0x400 || dev.rpm == 0xffff)
		dev.rpm = 0;

if(satadebug) {
	dprint("model %q\n", disk.model);
	dprint("serial %q\n", disk.serial);
	dprint("firmware %q\n", disk.firmware);
	dprint("sectors %llud\n", disk.sectors);
	dprint("size %llud bytes, %llud gb\n", disk.sectors*512, disk.sectors*512/(1024*1024*1024));
	dprint("ata/atapi versions %hux/%hux\n", dev.major, dev.minor);
	dprint("sectorflags:%s%s\n",
		(dev.sectorflags & MultiLogicalSectors) ? " MultiLogicalSectors" : "",
		(dev.sectorflags & LargeLogicalSectors) ? " LargeLogicalSectors" : "");
	dprint("logical sector size %d\n", dev.sectorsize);
	dprint("sata cap:%s%s%s\n",
		(dev.satacap & SataCapNCQ) ? " ncq" : "",
		(dev.satacap & SataCapGen2) ? " 3.0gbps" : "",
		(dev.satacap & SataCapGen1) ? " 1.5gbps" : "");
	dprint("cmdset %04hux %04hux %04hux %04hux %04hux %04hux\n",
		dev.cmdset[0], dev.cmdset[1], dev.cmdset[2], dev.cmdset[3], dev.cmdset[4], dev.cmdset[5]);
	dprint("            disabled: %04hux %04hux %04hux\n",
		dev.cmdset[0] & ~dev.cmdset[3+0],
		dev.cmdset[1] & ~dev.cmdset[3+1],
		dev.cmdset[2] & ~dev.cmdset[3+2]);
	dprint("wwn %llux\n", dev.wwn);
	dprint("nvcache cap:%s%s%s\n",
		(dev.nvcachecap & NvcacheEnabled) ? " NvcacheEnabled" : "",
		(dev.nvcachecap & NvcachePMEnabled) ? " NvcachePMEnabled" : "",
		(dev.nvcachecap & NvcachePMSup) ? " NvcachePMSup" : "");
	dprint("nvcache lblocks: %lud\n", dev.nvcachelblocks);
	dprint("rpm %hud\n", dev.rpm);
}
	satadir[Qdata].length = disk.sectors*512;
	disk.valid = 1;

	return 0;
}

static void
flush(void)
{
	atacmd(0xea, 0, 0, 0, 0, Nodata, nil);
}

static void
atadump(void)
{
	ulong n;
	char *buf, *p, *e;
	AtaReg *a = ATA1REG;

	n = 2048;
	p = buf = smalloc(n);
	e = p+n;

	p = seprint(p, e, "ata:\n");
	p = seprint(p, e, " data          %04lux\n", a->data);
	p = seprint(p, e, " feat/error    %02lux\n", a->feat);
	p = seprint(p, e, " sectors       %02lux\n", a->sectors);
	p = seprint(p, e, " lbalow        %02lux\n", a->lbalow);
	p = seprint(p, e, " lbamid        %02lux\n", a->lbamid);
	p = seprint(p, e, " lbahigh       %02lux\n", a->lbahigh);
	p = seprint(p, e, " dev           %02lux\n", a->dev);
	p = seprint(p, e, " status/cmd    %02lux\n", a->cmd);
	p = seprint(p, e, " altstatus/ctl %02lux\n", a->ctl);
	USED(p);

	dprint("%s", buf);

	free(buf);
}


static void
satareset(void)
{
	SatahcReg *hr = SATAHCREG;
	SataReg *sr = SATA1REG;

	/* power up sata1 port */
	CPUCSREG->mempm &= ~Sata1mem;
	CPUCSREG->clockgate |= Sata1clock;
	regreadl(&CPUCSREG->clockgate);

	/* disable interrupts */
	hr->intrmainena = 0;
	sr->edma.intreena = 0;
	sr->ifc.serrintrena = 0;
	sr->ifc.fisintrena = 0;

	/* clear interrupts */
	sr->edma.intre = 0;
	/* xxx more */

	/* disable & abort edma, bdma */
	sr->edma.cmd = (sr->edma.cmd & ~EdmaEnable) | EdmaAbort;

	/* xxx should set full register? */
	sr->ifc.ifccfg &= ~Physhutdown;

	/* xxx reset more registers */
	hr->cfg = (0xff<<0)		/* default mbus arbiter timeout value */
			| (1<<8)	/* no dma byte swap */
			| (1<<9)	/* no edma byte swap */
			| (1<<10)	/* no prdp byte swap */
			| (1<<16);	/* mbus arbiter timer disabled */
	hr->intrcoalesc = 0; /* raise interrupt after 0 completions (disable coalescing) */
	hr->intrtime = 0; /* number of clocks before asserting interrupt (disable coalescing) */
	hr->intr = 0;  /* clear */

/* xxx should set windows correct too */
if(0) {
	hr->win[0].ctl = (1<<0)		/* enable window */
			| (0<<1)	/* mbus write burst limit.  0: no limit (max 128 bytes), 1: do not cross 32 byte boundary */
			| ((0 & 0x0f)<<4)	/* target */
			| ((0xe & 0xff)<<8)	/* target attributes */
			| ((0xfff & 0xffff)<<16);	/* size of window, number+1 64kb units */
	hr->win[0].base = 0x0 & 0xffff;
}

	reqs = xspanalloc(32*sizeof reqs[0], 32*sizeof reqs[0], 0);
	resps = xspanalloc(32*sizeof resps[0], 32*sizeof resps[0], 0);
	prds = xspanalloc(32*8*sizeof prds[0], 16, 0);
	if(reqs == nil || resps == nil || prds == nil)
		panic("satareset");
	memset(reqs, 0, 32*sizeof reqs[0]);
	memset(resps, 0, 32*sizeof resps[0]);
	memset(prds, 0, 32*8*sizeof prds[0]);
	reqnext = respnext = 0;

	intrenable(Irqlo, IRQ0sata, sataintr, nil, "sata");
}

static void
satainit(void)
{
	SatahcReg *hr = SATAHCREG;
	SataReg *sr = SATA1REG;
	AtaReg *ar = ATA1REG;
	int n;
	int i;

	diprint("satainit...\n");

	tagnext = tagsinuse = 0;
	for(i = 0; i < nelem(tags); i++)
		tags[i] = i;

	/* disable interrupts */
	hr->intrmainena = 0;
	sr->edma.intreena = 0;
	sr->ifc.serrintrena = 0;
	sr->ifc.fisintrena = 0;

	/* disable & abort edma */
	sr->edma.cmd = (sr->edma.cmd & ~EdmaEnable) | EdmaAbort;

	/* clear edma */
	sr->edma.reqbasehi = sr->edma.respbasehi = 0;
	sr->edma.reqin = 0;
	sr->edma.reqout = 0;
	sr->edma.respin = 0;
	sr->edma.respout = (ulong)&resps[0];

	diprint("satainit, reqin %#lux, reqout %#lux\n", sr->edma.reqin, sr->edma.reqout);

if(0) {
	dprint("ata before reset\n");
	atadump();
}

	dprint("sr->ifc.sstatus before edma reset %#lux\n", sr->ifc.sstatus);

	sr->edma.cmd |= Atareset;
	delay(1);
	sr->edma.cmd &= ~Atareset;
	delay(200);

	/* errata magic, to fix the phy.  see uboot code (no docs available).  */
	sr->ifc.phym3 = (sr->ifc.phym3 & ~0x78100000) | 0x28000000;
	sr->ifc.phym4 = (sr->ifc.phym4 & ~1) | (1<<16);
	sr->ifc.phym9g2 = (sr->ifc.phym9g2 & ~0x400f) | 0x00008; /* tx driver amplitude */
	sr->ifc.phym9g1 = (sr->ifc.phym9g1 & ~0x400f) | 0x00008; /* tx driver amplitude */
	delay(100);  /* needed? */

	diprint("before phy init, sstatus %#lux, serror %#lux\n", sr->ifc.sstatus, sr->ifc.serror);

	sr->ifc.scontrol = CDETcomm|CSPDany|CIPMnopartial|CIPMnoslumber;
	regreadl(&sr->ifc.scontrol);
	delay(1);
	dprint("sr->ifc.sstatus after phy reset %#lux\n", sr->ifc.sstatus);

	sr->ifc.scontrol &= ~CDETcomm;
	regreadl(&sr->ifc.scontrol);
	microdelay(20*1000);

	/* check phy status */
	n = 0;
	while((sr->ifc.sstatus & SDETmask) != SDETdevphy) {
		if(n++ > 200) {
			dprint("no sata disk attached (sstatus %#lux; serror %#lux), aborting sata init\n", sr->ifc.sstatus, sr->ifc.serror);
			return;
		}
		delay(1);
	}

	diprint("after phy init, have connection, sstatus %#lux, serror %#lux\n", sr->ifc.sstatus, sr->ifc.serror);

	sr->ifc.ifccfg &= ~Ignorebsy;

	dprint("sr->ifc.sstatus before ata identify %#lux\n", sr->ifc.sstatus);

	/* xxx horrible, should properly wait during command execution, until device no longer busy */
	i = 0;
	for(;;) {
		if((ar->status & (Absy|Adrq)) == 0)
			break;
		if(++i == 20) {
			dprint("disk not ready\n");
			return;
		}
		tsleep(&up->sleep, return0, nil, 100);
	}
	if(identify() < 0) {
		dprint("no disk\n");
		return;
	}
	dprint("#S/sd01: %q, %lludGB (%,llud bytes), sata-i%s\n", disk.model, disk.sectors*512/(1024*1024*1024), disk.sectors*512, (SATA1REG->ifc.sstatus & SSPDgen2) ? "i" : "");

	hr->intrmainena = Sata1err|Sata1done|Sata1dmadone;
	hr->intr = 0;
	sr->edma.intre = 0;

	diprint("satainit, before edma, hr->intr %#lux, hr->intrmain %#lux, sr->edma.intre %#lux\n", hr->intr, hr->intrmain, sr->edma.intre);
}

static char *dets[] = {"none", "dev", nil, "devphy", "nophy"};
static struct {
	ulong	v;
	char	*s;
} serrors[] = {
	{EM,	"M"},
	{EN,	"N"},
	{EW,	"W"},
	{EB,	"B"},
	{ED,	"D"},
	{EC,	"C"},
	{EH,	"H"},
	{ES,	"S"},
	{ET,	"T"},
	{EX,	"X"},
};

/* hc intr */
enum {
	Dma1done	= 1<<1,
	Intrcoalesc	= 1<<4,
	Dev1intr	= 1<<9,
};
/* yuck, remove later */
static ulong
satadump(char *dst, long n, vlong off)
{
	char *buf, *p, *e, *s;
	SataReg *sr = SATA1REG;
	SatahcReg *hr = SATAHCREG;
	AtaReg *a = ATA1REG;
	ulong v;
	int i;
	ulong *w;

	USED(a);

	p = buf = smalloc(2048);
	e = p+n;

	p = seprint(p, e, "hc cfg    %#lux\n", hr->cfg);

	v = hr->intr;
	p = seprint(p, e, "hc intr   %#lux\n", v);
	if(v & Dma1done) p = seprint(p, e, "  dma1done");
	if(v & Intrcoalesc) p = seprint(p, e, "  intrcoalesc");
	if(v & Dev1intr) p = seprint(p, e, "  dev1intr");
	v &= ~(Dma1done|Intrcoalesc|Dev1intr);
	if(v) p = seprint(p, e, "  other: %#lux", v);
	p = seprint(p, e, "\n");

	v = hr->intrmain;
	p = seprint(p, e, "hc intrmain %#lux, ena %#lux\n", v, hr->intrmainena);
	if(v & Sata1err) p = seprint(p, e, "  sata1err");
	if(v & Sata1done) p = seprint(p, e, "  sata1done");
	if(v & Sata1dmadone) p = seprint(p, e, "  sata1dmadone");
	if(v & Satacoaldone) p = seprint(p, e, "  satacoaldone");
	v &= ~(Sata1err|Sata1done|Sata1dmadone|Satacoaldone);
	if(v) p = seprint(p, e, "  other: %#lux", v);
	p = seprint(p, e, "\n");

	p = seprint(p, e, "ncqdone   %#lux\n", sr->edma.ncqdone);

	v = sr->ifc.ifccfg;
	p = seprint(p, e, "ifccfg    %#lux\n", v);
	if(v & SSC) p = seprint(p, e, " ssc");
	if(v & Gen2) p = seprint(p, e, " gen2en");
	if(v & Comm) p = seprint(p, e, " comm");
	if(v & Physhutdown) p = seprint(p, e, " physhutdown");
	if(v & Emphadj) p = seprint(p, e, " emphadj");
	if(v & Emphtx) p = seprint(p, e, " emphtx");
	if(v & Emphpre) p = seprint(p, e, " emphpre");
	p = seprint(p, e, "\n");

	v = sr->edma.cfg;
	p = seprint(p, e, "cfg       %#lux", v);
	if(v & ECFGncq) p = seprint(p, e, " (ncq)");
	if(v & ECFGqueue) p = seprint(p, e, " (queued)");
	p = seprint(p, e, "\n");

	v = sr->edma.intre;
	p = seprint(p, e, "intre     %#lux, enabled %#lux\n", v, sr->edma.intreena);
	if(v) {
		if(v & Edeverr) p = seprint(p, e, " deverr");
		if(v & Edevdis) p = seprint(p, e, " devdis");
		if(v & Edevcon) p = seprint(p, e, " devcon");
		if(v & Eserror) p = seprint(p, e, " serror");
		if(v & Eselfdis) p = seprint(p, e, " selfdis");
		if(v & Etransint) p = seprint(p, e, " transint");
		if(v & Eiordy) p = seprint(p, e, " iordy");
		if(v & (1<<31)) p = seprint(p, e, " transerr");
		p = seprint(p, e, " rx %#lux %#lux, tx %#lux %#lux", (v>>13)&0xf, (v>>17)&0xf, (v>>21)&0xf, (v>>26)&0xf);
		p = seprint(p, e, "\n");
	}

	v = sr->edma.cmd;
	p = seprint(p, e, "cmd       %#lux\n", v);
	if(v & EdmaEnable) p = seprint(p, e, " edma enable\n");

	v = sr->edma.status;
	p = seprint(p, e, "status    %#lux\n", v);

	p = seprint(p, e, "req       %#lux %#lux\n", sr->edma.reqin, sr->edma.reqout);
	p = seprint(p, e, "resp      %#lux %#lux\n", sr->edma.respin, sr->edma.respout);

	v = sr->ifc.sstatus;
	p = seprint(p, e, "sstatus   %#lux\n", v);
	s = "unknown";
	switch(v&0xf) {
	case SDETnone:
	case SDETdev:	
	case SDETdevphy:
	case SDETnophy:
		if((v&0xf) < nelem(dets))
			s = dets[v&0xf];
		break;
	}
	p = seprint(p, e, "  det: %s", s);
	p = seprint(p, e, "  speed:");
	if(v & SSPDgen1) p = seprint(p, e, " gen1");
	if(v & SSPDgen2) p = seprint(p, e, " gen2");
	p = seprint(p, e, "  ipm: %#lux\n", (v>>8)&0xf);

	v = sr->ifc.serror;
	p = seprint(p, e, "serror    %#lux  ", v);
	for(i = 0; i < nelem(serrors); i++)
		if(v & serrors[i].v)
			p = seprint(p, e, "%s", serrors[i].s);
	p = seprint(p, e, "\n");

	p = seprint(p, e, "scontrol  %#lux\n", sr->ifc.scontrol);
	
	p = seprint(p, e, "ifcctl     %#lux\n", sr->ifc.ifcctl);
	p = seprint(p, e, "ifctestctl %#lux\n", sr->ifc.ifctestctl);
	v = sr->ifc.ifcstatus;
	p = seprint(p, e, "ifcstatus  %#lux\n", v);
	p = seprint(p, e, "  fistype  %#lux, pmrx %#lux, transfsm %#lux\n", v&0xff, (v>>8)&0xf, (v>>24)&0xf);
	if(v & (1<<12)) p = seprint(p, e, "  vendoruqdn");
	if(v & (1<<13)) p = seprint(p, e, "  vendoruqerr");
	if(v & (1<<14)) p = seprint(p, e, "  mbistrdy");
	if(v & (1<<15)) p = seprint(p, e, "  mbistfail");
	if(v & (1<<16)) p = seprint(p, e, "  abortcmd");
	if(v & (1<<17)) p = seprint(p, e, "  lbpass");
	if(v & (1<<18)) p = seprint(p, e, "  dmaact");
	if(v & (1<<19)) p = seprint(p, e, "  pioact");
	if(v & (1<<20)) p = seprint(p, e, "  rxhdact");
	if(v & (1<<21)) p = seprint(p, e, "  txhdact");
	if(v & (1<<22)) p = seprint(p, e, "  plugin");
	if(v & (1<<23)) p = seprint(p, e, "  linkdown");
	if(v & (1<<30)) p = seprint(p, e, "  rxbist");
	if(v & (1<<31)) p = seprint(p, e, "  N");
	p = seprint(p, e, "\n");

	p = seprint(p, e, "fiscfg   %#lux\n", sr->ifc.fiscfg);
	p = seprint(p, e, "fisintr  %#lux, ena %#lux\n", sr->ifc.fisintr, sr->ifc.fisintrena);
	w = sr->ifc.fis;
	p = seprint(p, e, "fis[7]   %#lux %#lux %#lux %#lux %#lux %#lux %#lux\n",
		w[0], w[1], w[2], w[3], w[4], w[5], w[6]);

if(0) {
	p = seprint(p, e, "pll      0x%08lux\n", sr->ifc.pllcfg);
	p = seprint(p, e, "ltmode   0x%08lux\n", sr->ifc.ltmode);
	p = seprint(p, e, "phym3    0x%08lux\n", sr->ifc.phym3);
	p = seprint(p, e, "phym4    0x%08lux\n", sr->ifc.phym4);
	p = seprint(p, e, "phym1    0x%08lux\n", sr->ifc.phym1);
	p = seprint(p, e, "phym2    0x%08lux\n", sr->ifc.phym2);
	p = seprint(p, e, "bistctl  0x%08lux\n", sr->ifc.bistctl);
	p = seprint(p, e, "bist1    0x%08lux\n", sr->ifc.bist1);
	p = seprint(p, e, "bist2    0x%08lux\n", sr->ifc.bist2);
	p = seprint(p, e, "vendor   0x%08lux\n", sr->ifc.vendor);
	p = seprint(p, e, "phym9g2  0x%08lux\n", sr->ifc.phym9g2);
	p = seprint(p, e, "phym9g1  0x%08lux\n", sr->ifc.phym9g1);
	p = seprint(p, e, "phycfg   0x%08lux\n", sr->ifc.phycfg);
	p = seprint(p, e, "phytctl  0x%08lux\n", sr->ifc.phytctl);
	p = seprint(p, e, "phym10   0x%08lux\n", sr->ifc.phym10);
	p = seprint(p, e, "phym12   0x%08lux\n", sr->ifc.phym12);
}

if(1) {
	p = seprint(p, e, "ata:\n");
	p = seprint(p, e, " data       %04lux\n", a->data);
	p = seprint(p, e, " feat/error %02lux\n", a->feat);
	p = seprint(p, e, " sectors    %02lux\n", a->sectors);
	p = seprint(p, e, " lbalow     %02lux\n", a->lbalow);
	p = seprint(p, e, " lbamid     %02lux\n", a->lbamid);
	p = seprint(p, e, " lbahigh    %02lux\n", a->lbahigh);
	p = seprint(p, e, " dev        %02lux\n", a->dev);
	p = seprint(p, e, " cmd/status %02lux\n", a->cmd);
	p = seprint(p, e, " ctl        %02lux\n", a->ctl);
}

	USED(p);
	n = readstr(off, dst, n, buf);
	free(buf);
	return n;
}

static int
isdone(void *p)
{
	ulong *v = p;
	return *v;
}

static int
tagfree(void *)
{
	return tagsinuse < nelem(tags);
}

static long
min(long a, long b)
{
	return (a < b) ? a : b;
}

static void
prdfill(Prd *prd, uchar *buf, long n)
{
	long nn;

	for(;;) {
		prd->addrlo = (ulong)buf;
		nn = min(n, 64*1024);
		prd->flagcount = nn & ((1<<16)-1);
		n -= nn;
		prd->addrhi = 0;
		if(n == 0) {
			prd->flagcount |= Endoftable;
			break;
		}
		prd++;
		buf += 64*1024;
	}
}

enum {
	/* first param for io() */
	Read, Write,
};
static ulong
io(int t, void *buf, long nb, vlong off)
{
	SataReg *sr = SATA1REG;
	Req *rq;
	int i;
	ulong tag;
	ulong ns;
	ulong dev;
	ulong nslo, nshi;
	uvlong lba;
	ulong lbalo, lbahi;
	ulong cmds[] = {0x60, 0x61}; /* xxx this is for sata fpdma only, ncq */
	Prd *prd;

	if(disk.valid == 0)
		error(Enodisk);

	if(nb < 0 || off < 0 || off % 512 != 0)
		error(Ebadarg);
	ns = nb/512;
	lba = off/512;
	if(nb == 0)
		return 0;
	if(nb % 512 != 0)
		error(Ebadarg);
	if((ulong)buf & 1)
		error(Ebadarg); /* fix, should alloc buffer and copy it afterwards? */

	if(lba == disk.sectors)
		return 0;
	if(lba > disk.sectors)
		error(Ebadarg);

	qlock(&reqsl);

	sleep(&tagl, tagfree, nil);
	tag = tags[tagnext];
	tagnext = (tagnext+1)%nelem(tags);
	tagsinuse++;

	i = reqnext;
	rq = &reqs[i];
	reqnext = (reqnext+1)%32;

	rq->prdhi = 0;
	if(ns > 128) {
		prd = &prds[i*8];
		if(ns > 8*128)
			ns = 8*128;
		prdfill(prd, buf, ns*512);
		dcwb(prd, 8*sizeof prd[0]);
		rq->prdlo = (ulong)prd;
		rq->ctl = 0;
		rq->count = 0;
	} else {
		rq->prdlo = (ulong)buf;
		rq->ctl = Rprdsingle;
		rq->count = (ns*512) & ((1<<16)-1); /* 0 means 64k */
	}
	if(t == Read)
		rq->ctl |= Rdev2mem;
	rq->ctl |= tag<<Rdevtagshift;
	rq->ctl |= tag<<Rhosttagshift;

	lbalo = lba>>0 & MASK(24);
	lbahi = lba>>24 & MASK(24);
	nslo = ns>>0 & 0xff;
	nshi = ns>>8 & 0xff;
	dev = 1<<6;
	rq->ata[0] = cmds[t]<<16 | nslo<<24;  /* cmd, feat current */
	rq->ata[1] = lbalo<<0 | dev<<24;  /* 24 bit lba current, dev */
	rq->ata[2] = lbahi<<0 | nshi<<24;  /* 24 bit lba previous, feat ext/previous */
	rq->ata[3] = (tag<<3)<<0 | 0<<8;  /* sectors current (tag), previous */
	dcwbinv(rq, sizeof rq[0]);

	reqsdone[tag] = 0;
	sr->edma.reqin = (ulong)&reqs[reqnext];
	if((sr->edma.cmd & EdmaEnable) == 0) {
		/* clear edma intr error, and satahc interrupt dmaXdone */

		sr->edma.cfg = (sr->edma.cfg & ~ECFGqueue) | ECFGncq;

		sr->ifc.fisintr = ~0;
		sr->ifc.fisintrena = 0;
		sr->ifc.fiscfg = (1<<6)-1;

		sr->edma.cmd = EdmaEnable;
		regreadl(&sr->edma.cmd);
	}
	qunlock(&reqsl);

	/* xxx have to return tag on interrupt? */
	tsleep(&reqsr[tag], isdone, &reqsdone[tag], 10*1000);

	if(!reqsdone[tag]) {
		/* xxx should do ata reset, or return the tag back to pool */
		error(Etimeout);
	}

	tags[(tagnext-tagsinuse+nelem(tags)) % nelem(tags)] = tag;
	tagsinuse--;
	wakeup(&tagl);

	/* xxx check for error, raise it */

	return ns*512;
}

static long
ctl(char *buf, long n)
{
	Cmdbuf *cb;

	cb = parsecmd(buf, n);
	if(strcmp(cb->f[0], "debug") == 0) {
		if(cb->nf != 2)
			error(Ebadarg);
		satadebug = atoi(cb->f[1]);
		return n;
	}
	if(strcmp(cb->f[0], "reset") == 0) {
		if(cb->nf != 1)
			error(Ebadarg);
		satainit();
		return n;
	}
	if(strcmp(cb->f[0], "identify") == 0) {
		if(identify() < 0) {
			error("no disk");
		}
		dprint("#S/sd01: %q, %lludGB (%,llud bytes), sata-i%s\n", disk.model, disk.sectors*512/(1024*1024*1024), disk.sectors*512, (SATA1REG->ifc.sstatus & SSPDgen2) ? "i" : "");
		return n;
	}
	error("bad ctl");
	return -1;
}


static int
satagen(Chan *c, char*, Dirtab *, int, int i, Dir *dp)
{
	Dirtab *tab;
	int ntab;

	tab = satadir;
	if(i != DEVDOTDOT){
		tab = &tab[c->qid.path+1];
		ntab = 1;
		if(c->qid.path == Qctlr)
			ntab = 3;
		if(i >= ntab)
			return -1;
		tab += i;
	}
	devdir(c, tab->qid, tab->name, tab->length, eve, tab->perm, dp);
	return 1;
}

static Chan*
sataattach(char *spec)
{
	return devattach('S', spec);
}

static Walkqid*
satawalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, satadir, nelem(satadir), satagen);
}

static int
satastat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, satadir, nelem(satadir), satagen);
}

static Chan*
sataopen(Chan *c, int omode)
{
	return devopen(c, omode, satadir, nelem(satadir), satagen);
}

static void	 
sataclose(Chan*)
{
}

static long	 
sataread(Chan *c, void *buf, long n, vlong off)
{
	char *p, *s, *e;
	long r, nn;

	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, satadir, nelem(satadir), satagen);

	switch((ulong)c->qid.path){
	case Qctl:
		if(disk.valid == 0)
			error(Enodisk);
		s = p = smalloc(1024);
		e = s+1024;
		p = seprint(p, e, "inquiry %q %q\n", "", disk.model); /* manufacturer unknown */
		p = seprint(p, e, "config serial %q firmware %q\n", disk.serial, disk.firmware);
		p = seprint(p, e, "geometry %llud %d\n", disk.sectors, 512);
		p = seprint(p, e, "part data %llud %llud\n", 0ULL, disk.sectors);
		USED(p);
		n = readstr(off, buf, n, s);
		free(s);
		return n;
	case Qdata:
		dcwbinv(buf, n);
		r = 0;
		while(r < n) {
			nn = io(Read, (uchar*)buf+r, n-r, off+r);
			if(nn == 0)
				break;
			r += nn;
		}
		return r;
	case Qtest:
		return satadump(buf, n, off);
	}
	error(Egreg);
	return 0;		/* not reached */
}

static long	 
satawrite(Chan *c, void *buf, long n, vlong off)
{
	long r, nn;

	switch((ulong)c->qid.path){
	case Qctl:
		return ctl(buf, n);
	case Qdata:
		dcwbinv(buf, n);
		r = 0;
		while(r < n) {
			nn = io(Write, (uchar*)buf+r, n-r, off+r);
			if(nn == 0)
				break;
			r += nn;
		}
		return r;
	case Qtest:
		break;
	}
	error(Egreg);
	return 0;		/* not reached */
}

Dev satadevtab = {
	'S',
	"sata",

	satareset,
	satainit,
	devshutdown,
	sataattach,
	satawalk,
	satastat,
	sataopen,
	devcreate,
	sataclose,
	sataread,
	devbread,
	satawrite,
	devbwrite,
	devremove,
	devwstat,
	devpower,
};
