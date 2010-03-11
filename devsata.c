/*
first attempt at driver for the sata controller.
kirkwood has two ports, on the sheevaplug only the second port is in use.
for now we'll assume ncq "first party dma" read/write commands (very old sata disks won't work).

todo:
- proper locking, wait for free edma slot.
- cache flushes around dma
- error handling & propagating to caller.
- phy errata
- better detect ata support of drives
- interrupt coalescing
- fix ncq, return early responses (in different order than request queue)
- general ata commands
- use generic sd interface (#S;  we need partitions)
- hotplug
- abstract for multiple ports
- support for non-ncq drives, by normal dma commands?
- for large i/o requests, could have always two ops scheduled: less waiting, still no hogging.
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"io.h"

#define diprint	if(0)iprint

char Enodisk[] = "no disk";

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

static ulong satadone;
static QLock reqsl;
static Rendez reqsr[32];
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
	EdmaDisable	= 1<<1,		/* abort and disable edma */
	Atareset	= 1<<2,		/* reset sata transport, link, physical layers */
	EdmaFreeze	= 1<<4,		/* do not process new requests from queue */

	/* edma status */
	Tagmask		= (1<<5)-1,	/* of last commands */
	Dev2mem		= 1<<5,		/* direction of command */
	Cacheempty	= 1<<6,		/* edma cache empty */
	EdmaIdle	= 1<<7,		/* edma idle (but disk can have command pending) */
#define IOID(v)	(((v)>>16)&(1<<6)-1)	/* io id of last command */

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
	CIPMpartial	= 1<<8,		/* no transition to PARTIAL */
	CIPMslumber	= 1<<9,		/* no transition to SLUMBER */
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
	volatile SatahcReg *hr = SATAHCREG;
	volatile SataReg *sr = SATA1REG;
	ulong v, w;
	static int count = 0;
	ulong in, out;
	
	w = hr->intr;
	v = hr->intrmain;
	diprint("intr %#lux, main %#lux\n", w, v);
	if(v & Sata1err) {
		diprint("intre %#lux\n", sr->intre);
		diprint("m 1err\n");
	}
	if(v & Sata1done) {
		diprint("m 1done\n");
	}
	if(v & Sata1dmadone) {
		diprint("m 1dmadone\n");
	}
	hr->intr = 0; // clear
	sr->intre = 0; // clear

	if(sr->ncqdone)
		iprint("ncqdone %#lux\n", sr->ncqdone);
	diprint("ncqdone %#lux\n", sr->ncqdone);
	diprint("reqin %#lux reqout %#lux respin %#lux respout %#lux\n", sr->reqin, sr->reqout, sr->respin, sr->respout);

	/* xxx must clean resps from dcache */
	in = (sr->respin & MASK(8))/sizeof (Resp);
	out = (sr->respout & MASK(8))/sizeof (Resp);
	for(;;) {
		if(in == out)
			break;
		w = resps[in].idflags & MASK(5);
		if(0 && w != in)
			iprint("response, in %lud, tag %lud\n", in, w);
		/* determine which request is done.  maybe from ncqdone.  wakeup its caller. */
		/* xxx check tag in idflags? */
		satadone |= 1<<out;
		diprint("new resp out %lud (in %lud), satadone now %#lux\n", out, in, satadone);
		/* xxx check for error */
		wakeup(&reqsr[out]);
		out = (out+1)%32;
		sr->respout = (ulong)&resps[out];
	}

	/* xxx testing */
	if(count++ >= 100) {
		intrdisable(Irqlo, IRQ0sata, sataintr, nil, "sata1");
		diprint("no more intr for now\n");
	}

	intrclear(Irqlo, IRQ0sata);
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
	volatile SatahcReg *hr = SATAHCREG;
	volatile SataReg *sr = SATA1REG;
	volatile AtaReg *a = ATA1REG;
	ulong v;

	/* xxx sleep until edma is disabled or edma is idle (edma status, bit 7 (EDMAIdle).  then disable edma. */

	hr->intrmainmask &= ~(Sata1err|Sata1done|Sata1dmadone);

	/* xxx should do this once?  or not at all? */
	sr->ifccfg |= Ignorebsy;
	sr->fiscfg = 0;
	sr->fisintr = ~0;
	sr->fisintrmask = 0;

	//print("pio, status %#lux\n", a->status);
	//a->ctl = 1<<1;
	a->feat = feat;
	a->sectors = sectors;
	a->lbalow = (lba>>0) & 0xff;
	a->lbamid = (lba>>8) & 0xff;
	a->lbahigh = (lba>>16) & 0xff;
	a->dev = dev;
	a->cmd = cmd;
	delay(100);
	v = a->status;
if(0) {
	print("result, status %#lux\n", v);
	if(v & Aerr)	print("  err\n");
	if(v & Adrq)	print("  drq\n");
	if(v & Adf)	print("  df\n");
	if(v & Adrdy)	print("  drdy\n");
	if(v & Absy)	print("  bsy\n");
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

	hr->intrmainmask |= Sata1err|Sata1done|Sata1dmadone;
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

static int
identify(void)
{
	uchar c;
	uchar buf[512];
	int i;
	ulong v;
	int qdepth;

	atacmd(0xec, 0, 0, 0, 0, Dev2host, buf);

	c = 0;
	for(i = 0; i < 512; i++)
		c += buf[i];
	if(c != 0) {
		print("check byte for 'identify device' response invalid\n");
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

	v = g16(buf+83*2);
	if((v & (1<<10)) == 0) {
		print("lba48 not supported (word 83 %#lux)\n", v);
		return -1;
	}

	v = g16(buf+75*2);
	qdepth = 1+(v&MASK(5));
	USED(qdepth);

	/* xxx check ncq support, how? */

if(0) {
	print("model %q\n", disk.model);
	print("serial %q\n", disk.serial);
	print("firmware %q\n", disk.firmware);
	print("sectors %llud\n", disk.sectors);
	print("size %llud bytes\n", disk.sectors*512);
	print("size %llud gb\n", disk.sectors*512/(1024*1024*1024));
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
satainit(void)
{
	volatile SatahcReg *hr = SATAHCREG;
	volatile SataReg *sr = SATA1REG;

	CPUCSREG->mempm &= ~(1<<11); // power up port sata1
	CPUCSREG->clockgate |= 1<<15; // sata1 clock enable
	/* xxx should enable phy too, perhaps reset, and do the phy errata dance */

	if((sr->sstatus & SDETmask) != SDETdevphy) {
		print("no sata disk attached, skipping satainit\n");
		return;
	}

	SATAHCREG->intrmainmask &= ~(1<<8); // no interrupt coalescing for now

	reqs = xspanalloc(32*sizeof reqs[0], 32*sizeof reqs[0], 0);
	resps = xspanalloc(32*sizeof resps[0], 32*sizeof resps[0], 0);
	prds = xspanalloc(32*8*sizeof prds[0], 16, 0);
	if(reqs == nil || resps == nil || prds == nil)
		panic("satainit");
	memset(reqs, 0, 32*sizeof reqs[0]);
	memset(resps, 0, 32*sizeof resps[0]);
	memset(prds, 0, 32*8*sizeof prds[0]);
	reqnext = respnext = 0;

	sr->reqbasehi = sr->respbasehi = 0;
	sr->reqin = 0;
	sr->reqout = 0;
	sr->respin = 0;
	sr->respout = (ulong)&resps[0];

	diprint("satainit, reqin %#lux, reqout %#lux\n", sr->reqin, sr->reqout);

	if(identify() < 0) {
		print("no disk\n");
		return;
	}
	print("#S/sd01: %q, %lludGB (%,llud bytes), sata-i%s\n", disk.model, disk.sectors*512/(1024*1024*1024), disk.sectors*512, (SATA1REG->sstatus & SSPDgen2) ? "i" : "");

	hr->intrmainmask = Sata1err|Sata1done|Sata1dmadone;
	hr->intr = 0; // clear
	sr->intre = 0; // clear

	diprint("satainit, before edma, hr->intr %#lux, hr->intrmain %#lux, sr->intre %#lux\n", hr->intr, hr->intrmain, sr->intre);

	/* set interrupts */
	intrenable(Irqlo, IRQ0sata, sataintr, nil, "sata1");

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
	volatile SataReg *sr = SATA1REG;
	volatile SatahcReg *hr = SATAHCREG;
	volatile AtaReg *a = ATA1REG;
	ulong v;
	int i;

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
	p = seprint(p, e, "hc intrmain %#lux, mask %#lux\n", v, hr->intrmainmask);
	if(v & Sata1err) p = seprint(p, e, "  sata1err");
	if(v & Sata1done) p = seprint(p, e, "  sata1done");
	if(v & Sata1dmadone) p = seprint(p, e, "  sata1dmadone");
	if(v & Satacoaldone) p = seprint(p, e, "  satacoaldone");
	v &= ~(Sata1err|Sata1done|Sata1dmadone|Satacoaldone);
	if(v) p = seprint(p, e, "  other: %#lux", v);
	p = seprint(p, e, "\n");

	p = seprint(p, e, "ncqdone   %#lux\n", sr->ncqdone);

	v = sr->ifccfg;
	p = seprint(p, e, "ifccfg    %#lux\n", v);
	if(v & SSC) p = seprint(p, e, " ssc");
	if(v & Gen2) p = seprint(p, e, " gen2en");
	if(v & Comm) p = seprint(p, e, " comm");
	if(v & Physhutdown) p = seprint(p, e, " physhutdown");
	if(v & Emphadj) p = seprint(p, e, " emphadj");
	if(v & Emphtx) p = seprint(p, e, " emphtx");
	if(v & Emphpre) p = seprint(p, e, " emphpre");
	p = seprint(p, e, "\n");

	v = sr->cfg;
	p = seprint(p, e, "cfg       %#lux", v);
	if(v & ECFGncq) p = seprint(p, e, " (ncq)");
	if(v & ECFGqueue) p = seprint(p, e, " (queued)");
	p = seprint(p, e, "\n");

	v = sr->intre;
	p = seprint(p, e, "intre     %#lux, mask %#lux\n", v, sr->intremask);
	if(v) {
		if(v & Edeverr) p = seprint(p, e, " deverr");
		if(v & Edevdis) p = seprint(p, e, " devdis");
		if(v & Edevcon) p = seprint(p, e, " devcon");
		if(v & Eserror) p = seprint(p, e, " serror");
		if(v & Eselfdis) p = seprint(p, e, " selfdis");
		if(v & Etransint) p = seprint(p, e, " transint");
		if(v & Eiordy) p = seprint(p, e, " iodry");
		if(v & (1<<31)) p = seprint(p, e, " transerr");
		p = seprint(p, e, " rx %#lux %#lux, tx %#lux %#lux", (v>>13)&0xf, (v>>17)&0xf, (v>>21)&0xf, (v>>26)&0xf);
		p = seprint(p, e, "\n");
	}

	v = sr->cmd;
	p = seprint(p, e, "cmd       %#lux\n", v);
	if(v & EdmaEnable) p = seprint(p, e, " edma enable\n");

	v = sr->status;
	p = seprint(p, e, "status    %#lux\n", v);

	p = seprint(p, e, "req       %#lux %#lux\n", sr->reqin, sr->reqout);
	p = seprint(p, e, "resp      %#lux %#lux\n", sr->respin, sr->respout);

	v = sr->sstatus;
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

	v = sr->serror;
	p = seprint(p, e, "serror    %#lux  ", v);
	for(i = 0; i < nelem(serrors); i++)
		if(v & serrors[i].v)
			p = seprint(p, e, "%s", serrors[i].s);
	p = seprint(p, e, "\n");

	p = seprint(p, e, "scontrol  %#lux\n", sr->scontrol);
	
	p = seprint(p, e, "ifcctl     %#lux\n", sr->ifcctl);
	p = seprint(p, e, "ifctestctl %#lux\n", sr->ifctestctl);
	v = sr->ifcstatus;
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

	p = seprint(p, e, "fiscfg   %#lux\n", sr->fiscfg);
	p = seprint(p, e, "fisintr  %#lux, mask %#lux\n", sr->fisintr, sr->fisintrmask);
	p = seprint(p, e, "fis[7]   %#lux %#lux %#lux %#lux %#lux %#lux %#lux\n", sr->fis[0], sr->fis[1], sr->fis[2], sr->fis[3], sr->fis[4], sr->fis[5], sr->fis[6]);

if(0) {
	p = seprint(p, e, "pll      0x%08lux\n", sr->pllcfg);
	p = seprint(p, e, "ltmode   0x%08lux\n", sr->ltmode);
	p = seprint(p, e, "phym3    0x%08lux\n", sr->phym3);
	p = seprint(p, e, "phym4    0x%08lux\n", sr->phym4);
	p = seprint(p, e, "phym1    0x%08lux\n", sr->phym1);
	p = seprint(p, e, "phym2    0x%08lux\n", sr->phym2);
	p = seprint(p, e, "bistctl  0x%08lux\n", sr->bistctl);
	p = seprint(p, e, "bist1    0x%08lux\n", sr->bist1);
	p = seprint(p, e, "bist2    0x%08lux\n", sr->bist2);
	p = seprint(p, e, "vendor   0x%08lux\n", sr->vendor);
	p = seprint(p, e, "phym9g2  0x%08lux\n", sr->phym9g2);
	p = seprint(p, e, "phym9g1  0x%08lux\n", sr->phym9g1);
	p = seprint(p, e, "phycfg   0x%08lux\n", sr->phycfg);
	p = seprint(p, e, "phytctl  0x%08lux\n", sr->phytctl);
	p = seprint(p, e, "phym10   0x%08lux\n", sr->phym10);
	p = seprint(p, e, "phym12   0x%08lux\n", sr->phym12);
}


if(0) {
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
	return (satadone & *v) != 0;
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
	Read, Write,
};
static ulong
io(int t, void *buf, long nb, vlong off)
{
	volatile SataReg *sr = SATA1REG;
	Req *rq;
	int i;
	ulong v;
	ulong ns;
	ulong dev;
	ulong nslo, nshi;
	uvlong lba;
	ulong lbalo, lbahi;
	ulong cmds[] = {0x60, 0x61};
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
		error(Ebadarg); // fix, should alloc buffer and copy it afterwards?

	if(lba == disk.sectors)
		return 0;
	if(lba > disk.sectors)
		error(Ebadarg);

	qlock(&reqsl);
	/* xxx wait until edma/slot becomes available */

	i = reqnext;
	rq = &reqs[i];
	reqnext = (reqnext+1)%32;

	rq->prdhi = 0;
	if(ns > 128) {
		prd = &prds[i*8];
		if(ns > 8*128)
			ns = 8*128;
		prdfill(prd, buf, ns*512);
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
	rq->ctl |= i<<Rdevtagshift;
	rq->ctl |= i<<Rhosttagshift;

	lbalo = lba & MASK(24);
	lbahi = (lba>>24) & MASK(24);
	nslo = ns&0xff;
	nshi = (ns>>8)&0xff;
	dev = 1<<6;
	rq->ata[0] = (cmds[t]<<16)|(nslo<<24); // cmd, feat current
	rq->ata[1] = (lbalo<<0)|(dev<<24); // 24 bit lba current, dev
	rq->ata[2] = (lbahi<<0)|(nshi<<24); // 24 bit lba previous, feat ext/previous
	rq->ata[3] = ((i<<3)<<0)|(0<<8); // sectors current (tag), previous
	sr->fiscfg = (1<<6)-1;
	sr->fisintr = ~0;
	sr->fisintrmask = 0;

diprint("io, using slot %d, off %llud\n", i, off);
	v = 1<<i;
	satadone &= ~v;
	dcflushall();
	sr->reqin = (ulong)&reqs[reqnext];
	if((sr->cmd & EdmaEnable) == 0) {
		sr->cfg |= ECFGncq|0x1f; // 0x1f for queue depth?
		sr->cmd = EdmaEnable;
	}
	qunlock(&reqsl);

	sleep(&reqsr[i], isdone, &v);
	/* xxx check for error, raise it */

	return ns*512;
}

static long
ctl(char *buf, long n)
{
	Cmdbuf *cb;
	volatile SataReg *sr = SATA1REG;

	cb = parsecmd(buf, n);
	if(strcmp(cb->f[0], "reset") == 0) {
		sr->cmd = Atareset;
		sr->cmd &= ~Atareset;
		/* xxx much more */
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
		error(Ebadarg);
	case Qdata:
		r = 0;
		while(r < n) {
			nn = io(Write, (uchar*)buf+r, n-r, off+r);
			if(nn == 0)
				break;
			r += nn;
		}
		return r;
	case Qtest:
		return ctl(buf, n);
	}
	error(Egreg);
	return 0;		/* not reached */
}

Dev satadevtab = {
	'S',
	"sata",

	devreset,
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
