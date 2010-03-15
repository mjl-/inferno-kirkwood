#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"io.h"

static int debug = 0;
#define dprint	if(debug)print

char Enocard[] = "no card";

enum {
	SDOk		= 0,
	SDTimeout	= -1,
	SDCardbusy	= -2,
	SDError		= -3,
};

enum {
	/* transfer mode */
	TMautocmd12	= 1<<2,
	TMxfertohost	= 1<<4,
	TMswwrite	= 1<<6,

	/* cmd */
	Respmax		= (136+7)/8,
#define CMDresp(x)	((x)<<0)
	CMDrespnone	= 0<<0,
	CMDresp136	= 1<<0,
	CMDresp48	= 2<<0,
	CMDresp48busy	= 3<<0,

	CMDdatacrccheck	= 1<<2,
	CMDcmdcrccheck	= 1<<3,
	CMDcmdindexcheck	= 1<<4,
	CMDdatapresent	= 1<<5,
	CMDunexpresp	= 1<<7,
#define CMDcmd(x)	((x)<<8)

	/* hwstate */
	HScmdinhibit	= 1<<0,
	HScardbusy	= 1<<1,
	HStxactive	= 1<<8,
	HSrxactive	= 1<<9,
	HSfifofull	= 1<<12,
	HSfifoempty	= 1<<13,
	HSautocmd12active	= 1<<14,

	/* hostctl */
	HCpushpull		= 1<<0,
	HCcardtypemask		= 3<<1,
	HCcardtypememonly	= 0<<1,
	HCcardtypeioonly	= 1<<1,
	HCcardtypeiomem		= 2<<1,
	HCcardtypemmc		= 3<<1,
	HCbigendian		= 1<<3,
	HClsbfirst		= 1<<4,
	HCdatawidth4		= 1<<9,
	HChighspeed		= 1<<10,
#define HCtimeout(x)	(((x) & MASK(4))<<11)
	HCtimeoutenable		= 1<<15,

	/* swreset */
	SRresetall	= 1<<8,

	/* status */
	NScmdcomplete	= 1<<0,
	NSxfercomplete	= 1<<1,
	NSblockgapevent	= 1<<2,
	NSdmaintr	= 1<<3,
	NStxready	= 1<<4,
	NSrxready	= 1<<5,
	NScardintr	= 1<<8,
	NSreadwaiton	= 1<<9,
	NSfifo8wfull	= 1<<10,
	NSfifo8wavail	= 1<<11,
	NSsuspended	= 1<<12,
	NSautocmd12done	= 1<<13,
	NSunexpresp	= 1<<14,
	NSerror		= 1<<15,

	/* errstatus */
	EScmdtimeout	= 1<<0,
	EScmdcrc	= 1<<1,
	EScmdendbit	= 1<<2,
	EScmdindex	= 1<<3,
	ESdatatimeout	= 1<<4,
	ESrddatacrc	= 1<<5,
	ESrddataend	= 1<<6,
	ESautocmd12	= 1<<8,
	EScmdstartbit	= 1<<9,
	ESxfersize	= 1<<10,
	ESresptbit	= 1<<11,
	EScrcendbit	= 1<<12,
	EScrcstartbit	= 1<<13,
	EScrcstatus	= 1<<14,

	/* autocmd12 index */
	AIcheckbusy	= 1<<0,
	AIcheckindex	= 1<<1,
#define AICMDINDEX(x)	((x)<<8)
};

enum {
	CMD8pattern	= 0xaa,
	CMD8patternmask	= MASK(8),
	CMD8voltage	= 1<<8,
	CMD8voltagemask	= MASK(4)<<8,

	ACMD41voltagewindow	= MASK(23-15+1)<<15,
	ACMD41sdhcsupported	= 1<<30,
	ACMD41ready		= 1<<31,
};

enum {
	ESAcmd12Notexe		= 1<<0,
	ESAcmd12Timeout		= 1<<1,
	ESAcmd12CrcErr		= 1<<2,
	ESAcmd12EndBitErr	= 1<<3,
	ESAcmd12IndexErr	= 1<<4,
	ESAcmd12RespTBi		= 1<<5,
	ESAcmd12RespStartBitErr	= 1<<6,
};

typedef struct Card Card;
typedef struct Cid Cid;
typedef struct Csd Csd;

struct Cid
{
	uint	mid;		/* manufacturer id */
	char	oid[2+1];	/* oem id */
	char	prodname[5+1];	/* product name */
	uint	rev;		/* product revision */
	ulong	serial;
	int	year;
	int	mon;
};

/* xxx make shorter but still readable names */
struct Csd
{
	int	version;
	int	taac, nsac;
	int	xferspeed;
	int	cmdclasses;
	int	readblocklength, readblockpartial;
	int	writeblockmisalign, readblockmisalign;
	int	dsr;
	int	size;
	union {
		struct {
			int	vddrmin, vddrmax, vddwmin, vddwmax;
			int	sizemult;
		} v0;
	};
	int	eraseblockenable, erasesectorsize;
	int	wpgroupsize, wpgroupenable;
	int	writespeedfactor;
	int	writeblocklength, writeblockpartial;
	int	fileformatgroup;
	int	copy;
	int	permwriteprotect, tmpwriteprotect;
	int	fileformat;
};

struct Card {
	int	valid;
	Cid	cid;
	Csd	csd;
	ulong	bs;
	uvlong	size;
	int	mmc;
	int	sd2;
	int	sdhc;
	uint	rca;
	uchar	resp[Respmax];
	int	lastcmd;
	int	lastisapp;
	int	lasterr;
};

static Card card;
static Rendez dmar;

enum {
	Qdir,
	Qctl,
	Qinfo,
	Qdata,
};

static
Dirtab sdiotab[]={
	".",		{Qdir, 0, QTDIR},	0,	0555,
	"sdioctl",	{Qctl},			0,	0666,
	"sdioinfo",	{Qinfo},		0,	0444,
	"sdio",		{Qdata},		0,	0666,
};

static int
min(int a, int b)
{
	if(a < b)
		return a;
	return b;
}

static ulong
bits(uchar *p, int msb, int lsb)
{
	ulong v;
	int nbits, o, n;

	nbits = msb-lsb+1;
	n = lsb/8;
	p += n;
	lsb -= n*8;

	n = min(nbits, 8-lsb);
	nbits -= n;
	v = (*p++>>lsb) & MASK(n);
	o = n;

	while(nbits > 0) {
		n = min(nbits, 8);
		nbits -= n;
		v |= ((ulong)*p++ & MASK(n)) << o;
		o += n;
	}
	return v;
}

static void
putle(uchar *p, uvlong v, int n)
{
	while(n-- > 0) {
		*p++ = v;
		v >>= 8;
	}
}

static ulong
getresptype(int isapp, ulong cmd)
{
	if(isapp)
		return CMDresp48;

	switch(cmd) {
	case 2:
	case 9:
	case 10:
		return CMDresp136;
	case 7:
	case 12:
	case 28:
	case 29:
	case 38:
		return CMDresp48busy;
	case 0:
	case 4:
	case 15:
		return CMDrespnone;
	}
	return CMDresp48;
}

char *statusstrs[] = {
"cmdcomplete",
"xfercomplete",
"blockgapevent",
"dmaintr",
"txready",
"rxready",
"",
"",
"cardintr",
"readwaiton",
"fifo8wfull",
"fifo8wavail",
"suspended",
"autocmd12done",
"unexpresp",
"errorintr",
};
static char *
statusstr(char *p, char *e, ulong v)
{
	int i;
	for(i = 0; i < nelem(statusstrs); i++)
		if(v & (1<<i))
			p = seprint(p, e, " %q", statusstrs[i]);
	return p;
}

char *errstatusstrs[] = {
"cmdtimeout",
"cmdcrc",
"cmdendbit",
"cmdindex",
"datatimeout",
"rddatacrc",
"rddataend",
"",
"autocmd12",
"cmdstartbit",
"xfersize",
"resptbit",
"crcendbit",
"crcstartbit",
"crcstatus",
};
static char *
errstatusstr(char *p, char *e, ulong v)
{
	int i;
	for(i = 0; i < nelem(errstatusstrs); i++)
		if(v & (1<<i))
			p = seprint(p, e, " %q", errstatusstrs[i]);
	return p;
}

char *acmd12errstatusstrs[] = {
"acmd12notexe",
"acmd12timeout",
"acmd12crcerr",
"acmd12endbiterr",
"acmd12indexerr",
"acmd12resptbi",
"acmd12respstartbiterr",
};

static char *
acmd12errstatusstr(char *p, char *e, ulong v)
{
	int i;
	for(i = 0; i < nelem(acmd12errstatusstrs); i++)
		if(v & (1<<i))
			p = seprint(p, e, " %q", acmd12errstatusstrs[i]);
	return p;
}

static void
printstatus(char *s, ulong status, ulong errstatus, ulong acmd12errstatus)
{
	char *p, *e;

	if(!debug)
		return;

	p = up->genbuf;
	e = p+sizeof (up->genbuf);

	p = statusstr(p, e, status);
	p = seprint(p, e, ";");
	p = errstatusstr(p, e, errstatus);
	p = seprint(p, e, ";");
	p = acmd12errstatusstr(p, e, acmd12errstatus);
	USED(p);

	print("status: %s%s\n", s, up->genbuf);
}


static void
sdclock(ulong v)
{
	SDIOREG->clockdiv = ((100*1000*1000)/v)-1;
}

static char *errstrs[] = {
"success",
"timeout",
"card busy",
"error",
};

static void
errorsd(char *s)
{
	if(card.lasterr == SDOk)
		errorf("%s (%scmd %d)", s, card.lastisapp ? "a" : "", card.lastcmd);
	else
		errorf("%s, %s for %scmd %d", s, errstrs[-card.lasterr], card.lastisapp ? "a" : "", card.lastcmd);
}

static int
dmafinished(void*)
{
	ulong need = NScmdcomplete|NSdmaintr;

	if((SDIOREG->status & need) == need) {
		dprint("dmadone\n");
		return 1;
	}
	dprint("not dmadone\n");
	SDIOREG->statusirqmask = NSdmaintr;
	return 0;
}

static int
sdcmd0(Card *c, int isapp, ulong cmd, ulong arg)
{
	SdioReg *reg = SDIOREG;
	int i, s;
	ulong resptype, cmdopts, txmode, v, need;
	uvlong w;

	i = 0;
	for(;;) {
		if((reg->hwstate & (HScmdinhibit|HScardbusy)) == 0)
			break;
		if(i++ >= 50)
			return SDCardbusy;
		tsleep(&up->sleep, return0, nil, 10);
	}

	/* "next command is sd application specific" */
	if(isapp && (s = sdcmd0(c, 0, 55, card.rca<<16)) < 0)
		return s;

	dprint("sdcmd, isapp %d, cmd %lud, arg %#lux\n", isapp, cmd, arg);

	/* clear status */
	reg->status = ~0;
	reg->errstatus = ~0;
	reg->acmd12errstatus = ~0;
	printstatus("before cmd: ", reg->status, reg->errstatus, reg->acmd12errstatus);

	microdelay(1000);
	/* prepare args & execute command */
	reg->argcmdlo = (arg>>0) & MASK(16);
	reg->argcmdhi = (arg>>16) & MASK(16);
	cmdopts = 0;
	txmode = 0;
	if(cmd == 17 || cmd == 18) {
		txmode = TMxfertohost;
		if(cmd == 18) {
			txmode |= TMautocmd12;
			reg->acmd12arglo = 0;
			reg->acmd12arghi = 0;
			reg->acmd12idx = AIcheckbusy|AIcheckindex|AICMDINDEX(12);
		}
		cmdopts = CMDdatapresent;
	}
	reg->txmode = txmode;
	resptype = getresptype(isapp, cmd);
	reg->cmd = CMDresp(resptype)|cmdopts|CMDcmd(cmd);

	if(cmd == 17 || cmd == 18) {
		/* wait for dma interrupt that signals completion */
		tsleep(&dmar, dmafinished, nil, 5000);

		need = NScmdcomplete|NSdmaintr;
		if((reg->status & need) != need || (reg->status & (NSerror|NSunexpresp)) != 0) {
			printstatus("dma err: ", reg->status, reg->errstatus, reg->acmd12errstatus);
			return SDError;
		}
	} else {
		/* poll for completion/error */
		need = NScmdcomplete;
		i = 0;
		for(;;) {
			v = reg->status;
			if(v & (NSerror|NSunexpresp)) {
				printstatus("error: ", v, reg->errstatus, reg->acmd12errstatus);
				if(reg->errstatus & EScmdtimeout)
					return SDTimeout;
				return SDError;
			}
			if((v & need) == need)
				break;
			if(i++ >= 100) {
				print("command unfinished\n");
				printstatus("timeout: ", v, reg->errstatus, reg->acmd12errstatus);
				return SDError;
			}
			tsleep(&up->sleep, return0, nil, 10);
		}
	}
	printstatus("success", reg->status, reg->errstatus, reg->acmd12errstatus);

	/* fetch the response */
	memset(c->resp, '\0', Respmax);
	switch(resptype) {
	case CMDrespnone:
		break;
	case CMDresp136:
		w = 0;
		w |= (uvlong)reg->resp[7] & MASK(14);
		w |= (uvlong)reg->resp[6]<<(0*16+14);
		w |= (uvlong)reg->resp[5]<<(1*16+14);
		w |= (uvlong)reg->resp[4]<<(2*16+14);
		w |= (uvlong)reg->resp[3]<<(3*16+14);
		putle(c->resp+1, w, 8);

		w = 0;
		w |= (uvlong)reg->resp[3]>>2;
		w |= (uvlong)reg->resp[2]<<(0*16+14);
		w |= (uvlong)reg->resp[1]<<(1*16+14);
		w |= (uvlong)reg->resp[0]<<(2*16+14);
		putle(c->resp+1+8, w, 8);
		break;
	case CMDresp48:
	case CMDresp48busy:
		w = 0;
		w |= (uvlong)reg->resp[2] & MASK(6);
		w |= (uvlong)reg->resp[1]<<(0*16+6);
		w |= (uvlong)reg->resp[0]<<(1*16+6);
		putle(c->resp+1, w, 4);
		break;
	}
	return 0;
}

static int
sdcmd(Card *c, ulong cmd, ulong arg)
{
	c->lastisapp = 0;
	c->lastcmd = cmd;
	c->lasterr = sdcmd0(c, 0, cmd, arg);
	return c->lasterr;
}

static int
sdacmd(Card *c, ulong cmd, ulong arg)
{
	c->lastisapp = 1;
	c->lastcmd = cmd;
	c->lasterr = sdcmd0(c, 1, cmd, arg);
	return c->lasterr;
}

static int
parsecid(Cid *c, uchar *r)
{
	uchar *p;

	c->mon		= bits(r, 11, 8);
	c->year		= 2000 + bits(r, 19, 16)*10 + bits(r, 15, 12);
	c->serial	= bits(r, 55, 24);
	c->rev		= bits(r, 63, 56);

	p = r+64/8;
	c->prodname[0] = p[4];
	c->prodname[1] = p[3];
	c->prodname[2] = p[2];
	c->prodname[3] = p[1];
	c->prodname[4] = p[0];
	c->prodname[5] = '\0';

	p = r+104/8;
	c->oid[0] = p[1];
	c->oid[1] = p[0];
	c->oid[2] = '\0';

	c->mid		= bits(r, 127, 120);
	return 0;
}

static char*
cidstr(char *p, char *e, Cid *c)
{
	return seprint(p, e,
		"product %s, rev %#ux, serial %#lux, made %04d-%02d, oem %s, manufacturer %#ux\n",
		c->prodname,
		c->rev,
		c->serial,
		c->year, c->mon,
		c->oid,
		c->mid);
}

static int
parsecsd(Csd *c, uchar *r)
{
	c->version = bits(r, 127, 126);
	if(c->version != 0 && c->version != 1)
		return -1;

	c->taac			= bits(r, 119, 112);
	c->nsac			= bits(r, 111, 104);
	c->xferspeed		= bits(r, 103, 96);
	c->cmdclasses		= bits(r, 95, 84);
	c->readblocklength	= bits(r, 83, 80);
	c->readblockpartial	= bits(r, 79, 79);
	c->writeblockmisalign	= bits(r, 78, 78);
	c->readblockmisalign	= bits(r, 77, 77);
	c->dsr			= bits(r, 76, 76);
	if(c->version == 0) {
		c->size		= bits(r, 75, 62);
		c->v0.vddrmin	= bits(r, 61, 59);
		c->v0.vddrmax	= bits(r, 58, 56);
		c->v0.vddwmin	= bits(r, 55, 53);
		c->v0.vddwmax	= bits(r, 52, 50);
		c->v0.sizemult	= bits(r, 49, 47);
	} else {
		c->size		= bits(r, 69, 48);
	}
	c->eraseblockenable	= bits(r, 46, 46);
	c->erasesectorsize	= bits(r, 45, 39);
	c->wpgroupsize		= bits(r, 38, 32);
	c->wpgroupenable	= bits(r, 31, 31);
	c->writespeedfactor	= bits(r, 28, 26);
	c->writeblocklength	= bits(r, 25, 22);
	c->writeblockpartial	= bits(r, 21, 21);
	c->fileformatgroup	= bits(r, 15, 15);
	c->copy			= bits(r, 14, 14);
	c->permwriteprotect	= bits(r, 13, 13);
	c->tmpwriteprotect	= bits(r, 12, 12);
	c->fileformat		= bits(r, 11, 10);

	return 0;
}

static char*
csdstr(char *p, char *e, Csd *c)
{
	char versbuf[128];

	versbuf[0] = '\0';
	if(c->version == 0)
		snprint(versbuf, sizeof versbuf,
			"vddrmin %x\n"
			"vddrmax %x\n"
			"vddwmin %x\n"
			"vddwmax %x\n"
			"sizemult %x\n",
			c->v0.vddrmin,
			c->v0.vddrmax,
			c->v0.vddwmin,
			c->v0.vddwmax,
			c->v0.sizemult);
	return seprint(p, e,
		"version %x\n"
		"taac %x\n"
		"nsac %x\n"
		"xferspeed %x\n"
		"cmdclasses %x\n"
		"readblocklength %x\n"
		"readblockpartial %x\n"
		"writeblockmisalign %x\n"
		"readblockmisalign %x\n"
		"dsr %x\n"
		"devsize %x\n"
		"%s"
		"eraseblockenable %x\n"
		"erasesectorsize %x\n"
		"wpgroupsize %x\n"
		"wpgroupenable %x\n"
		"writespeedfactor %x\n"
		"writeblocklength %x\n"
		"writeblockpartial %x\n"
		"fileformatgroup %x\n"
		"copy %x\n"
		"permwriteprotect %x\n"
		"tmpwriteprotect %x\n"
		"fileformat %x\n",
		c->version,
		c->taac,
		c->nsac,
		c->xferspeed,
		c->cmdclasses,
		c->readblocklength,
		c->readblockpartial,
		c->writeblockmisalign,
		c->readblockmisalign,
		c->dsr,
		c->size,
		versbuf,
		c->eraseblockenable,
		c->erasesectorsize,
		c->wpgroupsize,
		c->wpgroupenable,
		c->writespeedfactor,
		c->writeblocklength,
		c->writeblockpartial,
		c->fileformatgroup,
		c->copy,
		c->permwriteprotect,
		c->tmpwriteprotect,
		c->fileformat);
}

static char*
cardtype(Card *c)
{
	if(c->mmc)
		return "mmc";
	if(c->sdhc)
		return "sdhc";
	return "sd";
}

static char*
cardstr(Card *c, char *buf, int n)
{
	snprint(buf, n,
		"card %s\n"
		"type %s\n"
		"size %lld bytes\n"
		"blocksize %lud\n"
		"manufactured %d-%02d\n"
		"rev %#ux\n"
		"serial %#lux\n",
		c->cid.prodname,
		cardtype(c),
		c->size,
		c->bs,
		c->cid.year, c->cid.mon,
		c->cid.rev,
		c->cid.serial);
	return buf;
}

static void
sdinit(void)
{
	int i, s;
	ulong v;
	SdioReg *reg = SDIOREG;

	sdclock(400*1000);
	reg->hostctl &= ~HChighspeed;

	/* force card to idle state */
	if(sdcmd(&card, 0, 0) < 0)
		errorsd("reset failed");

	/*
	 * "send interface command".  only >=2.00 cards will respond.
	 * we send a check pattern and supported voltage range.
	 */
	card.mmc = 0;
	card.sd2 = 0;
	card.sdhc = 0;
	card.rca = 0;
	s = sdcmd(&card, 8, CMD8voltage|CMD8pattern);
	if(s == SDOk) {
		card.sd2 = 1;
		v = bits(card.resp+1, 31, 0);
		if((v & CMD8patternmask) != CMD8pattern)
			errorsd("sd check pattern mismatch");
		if((v & CMD8voltagemask) != CMD8voltage)
			errorsd("sd voltage not supported");
	} else if(s != SDTimeout)
		errorsd("voltage exchange failed");

	/*
	 * "send host capacity support information".
	 * we send supported voltages & our sdhc support.
	 * mmc cards won't respond.  sd cards will power up and indicate
	 * if they support sdhc.
	 */
	i = 0;
	for(;;) {
		v = ACMD41sdhcsupported|ACMD41voltagewindow;
		s = sdacmd(&card, 41, v);
		if(s == SDTimeout) {
			if(card.sd2)
				errorsd("sd >=2.00 card");
			card.mmc = 1;
			break;
		}
		if(s < 0)
			errorsd("exchange voltage/sdhc support info");
		v = bits(card.resp+1, 31, 0);
		if((v & ACMD41voltagewindow) == 0)
			errorsd("voltage not supported");
		if(v & ACMD41ready) {
			card.sdhc = (v & ACMD41sdhcsupported) != 0;
			break;
		}

		if(i >= 20)
			errorsd("sd card failed to power up");
		tsleep(&up->sleep, return0, nil, 10);
	}
	dprint("acmd41 done, mmc %d, sd2 %d, sdhc %d\n", card.mmc, card.sd2, card.sdhc);
	if(card.mmc)
		error("mmc cards not yet supported"); // xxx p14 says this involves sending cmd1

	if(sdcmd(&card, 2, 0) < 0)
		errorsd("card identification");
	if(parsecid(&card.cid, card.resp) < 0)
		errorsd("bad cid register");

	i = 0;
	for(;;) {
		if(sdcmd(&card, 3, 0) < 0)
			errorsd("getting relative address");
		card.rca = bits(card.resp+1, 31, 16);
		v = bits(card.resp+1, 15, 0);
		dprint("have card rca %ux, status %lux\n", card.rca, v);
		USED(v);
		if(card.rca != 0)
			break;
		if(i++ == 10)
			errorsd("card insists on invalid rca 0");
	}

	if(sdcmd(&card, 9, card.rca<<16) < 0)
		errorsd("get csd");
	if(parsecsd(&card.csd, card.resp) < 0)
		errorsd("bad csd register");

	if(card.csd.version == 0) {
		card.bs = 1<<card.csd.readblocklength;
		card.size = card.csd.size+1;
		card.size *= 1<<(card.csd.v0.sizemult+2);
		card.size *= 1<<card.csd.readblocklength;
		kprint("csd0, block length read/write %d/%d, size %lld bytes, eraseblock %d\n",
			1<<card.csd.readblocklength, 
			1<<card.csd.writeblocklength,
			card.size,
			(1<<card.csd.writeblocklength)*(card.csd.erasesectorsize+1));
	} else {
		card.bs = 512;
		card.size = (vlong)(card.csd.size+1)*card.bs*1024;
		kprint("csd1, fixed 512 block length, size %lld bytes, eraseblock fixed 512\n", card.size);
	}

	if(card.sdhc) {
		dprint("enabling sdhc & setting clock to 50mhz\n");
		sdclock(50*1000*1000);
		reg->hostctl |= HChighspeed;
	} else {
		dprint("leaving sdhc off & setting clock to 25mhz\n");
		sdclock(25*1000*1000);
	}

	if(sdcmd(&card, 7, card.rca<<16) < 0)
		errorsd("selecting card");

	/* xxx have to check if this is supported by card */
	if(sdacmd(&card, 6, (1<<1)) < 0)
		errorsd("setting buswidth to 4-bit");

	if(sdcmd(&card, 16, card.bs) < 0)
		errorsd("setting block length");

	card.valid = 1;
	kprint("%s", cardstr(&card, up->genbuf, sizeof (up->genbuf)));
}


static long
sdio(uchar *a, long n, vlong offset, int iswrite)
{
	SdioReg *reg = SDIOREG;
	ulong arg;

	if(iswrite)
		error("not yet implemented");

	if(!card.valid)
		error(Enocard);

	/* xxx we should cover this cases with a buffer, and then use the same code to allow non-sector-aligned reads? */
	if((ulong)a % 4 != 0)
		error("bad buffer alignment...");

	if(offset % card.bs != 0)
		error("not sector aligned");
	if(n % card.bs != 0)
		error("not multiple of sector size");

	dcwbinv(a, n);
	reg->dmaaddrlo = (ulong)a & MASK(16);
	reg->dmaaddrhi = ((ulong)a>>16) & MASK(16);
	reg->blksize = card.bs;
	reg->blkcount = n/card.bs;
	if(card.sdhc)
		arg = offset/card.bs;
	else
		arg = offset;
	//tsleep(&up->sleep, return0, nil, 250);
	dprint("sdio, a %#lux, dmaddrlo %#lux dmaadrhi %#lux blksize %lud blkcount %lud cmdarg %#lux\n",
		a, (ulong)a & MASK(16), ((ulong)a>>16) & MASK(16), card.bs, n/card.bs, arg);
	//tsleep(&up->sleep, return0, nil, 250);
	if(sdcmd(&card, 18, arg) < 0)
		errorsd(iswrite ? "writing" : "reading");

	return n;
}

static void
sdiointr(Ureg*, void*)
{
	SdioReg *reg = SDIOREG;
	char buf[128];
	char *p, *e;

	if(debug) {
		iprint("sdio intr %lux %lux %lux %lux\n",
			reg->status,
			reg->status & reg->statusirqmask,
			reg->errstatus,
			reg->errstatus & reg->errstatusirqmask);

		p = buf;
		e = p+sizeof (buf);

		p = statusstr(p, e, reg->status);
		p = seprint(p, e, ";");
		p = errstatusstr(p, e, reg->errstatus);
		USED(p);

		iprint("intr: %s\n", buf);
	}

	/*
	 * for now, interrupts are only used for dma transfers.
	 * don't clear the status, just make sure we are not called again
	 * before this interrupt is handled.
	 */
	wakeup(&dmar);
	reg->statusirqmask &= ~NSdmaintr;
	intrclear(Irqlo, IRQ0sdio);
}

static void
sdioreset(void)
{
	SdioReg *reg = SDIOREG;

	/* disable all interrupts.  dma interrupt will be enabled as required.  all bits lead to IRQ0sdio. */
	reg->statusirqmask = 0;
	reg->errstatusirqmask = 0;
	intrenable(Irqlo, IRQ0sdio, sdiointr, nil, "sdio");
}

static void
sdioinit(void)
{
	SdioReg *reg = SDIOREG;

	card.valid = 0;

	/* reset the bus, forcing all cards to idle state */
	reg->swreset = SRresetall;
	tsleep(&up->sleep, return0, nil, 50);

	sdclock(25*1000*1000);

	/* configure host controller */
	reg->hostctl = HCpushpull|HCcardtypememonly|HCbigendian|HCdatawidth4|HCtimeout(15)|HCtimeoutenable;

	/* clear status */
	reg->status = ~0;
	reg->errstatus = ~0;

	/* enable most status reporting */
	reg->statusmask = ~(NStxready|NSfifo8wavail);
	reg->errstatusmask = ~0;

	/* disable all interrupts.  dma interrupt will be enabled as required.  all bits lead to IRQ0sdio. */
	reg->statusirqmask = 0;
	reg->errstatusirqmask = 0;
}

static Chan*
sdioattach(char* spec)
{
	return devattach('o', spec);
}

static Walkqid*
sdiowalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, sdiotab, nelem(sdiotab), devgen);
}

static int
sdiostat(Chan* c, uchar *db, int n)
{
	return devstat(c, db, n, sdiotab, nelem(sdiotab), devgen);
}

static Chan*
sdioopen(Chan* c, int omode)
{
	return devopen(c, omode, sdiotab, nelem(sdiotab), devgen);
}

static void
sdioclose(Chan* c)
{
	USED(c);
}

static long
sdioread(Chan* c, void* a, long n, vlong offset)
{
	char *buf, *p, *e;

	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, sdiotab, nelem(sdiotab), devgen);
	case Qctl:
		cardstr(&card, up->genbuf, sizeof (up->genbuf));
		n = readstr(offset, a, n, up->genbuf);
		break;
	case Qinfo:
		if(card.valid == 0)
			error(Enocard);
		p = buf = smalloc(READSTR);
		e = p+READSTR;
		p = cidstr(p, e, &card.cid);
		p = csdstr(p, e, &card.csd);
		USED(p);
		n = readstr(offset, a, n, buf);
		free(buf);
		break;
	case Qdata:
		n = sdio(a, n, offset, 0);
		dprint("returning %ld bytes\n", n);
		break;
	default:
		n = 0;
		break;
	}
	return n;
}

enum {
	CMreset, CMinit, CMdebug,
};
static Cmdtab sdioctl[] = 
{
	CMreset,	"reset",	1,
	CMinit,		"init",		1,
	CMdebug,	"debug",	2,
};

static long
sdiowrite(Chan* c, void* a, long n, vlong offset)
{
	Cmdbuf *cb;
	Cmdtab *ct;

	switch((ulong)c->qid.path){
	case Qctl:
		if(!iseve())
			error(Eperm);

		cb = parsecmd(a, n);
		if(waserror()) {
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, sdioctl, nelem(sdioctl));
		switch(ct->index) {
		case CMreset:
			sdioinit();
			break;
		case CMinit:
			sdinit();
			break;
		case CMdebug:
			if(strcmp(cb->f[1], "on") == 0)
				debug = 1;
			else if(strcmp(cb->f[1], "off") == 0)
				debug = 0;
			else
				error(Ebadctl);
		}
		poperror();
		free(cb);
		break;
	case Qdata:
		n = sdio(a, n, offset, 1);
		break;
	default:
		error(Ebadusefd);
	}
	return n;
}

Dev sdiodevtab = {
	'o',
	"sdio",

	sdioreset,
	sdioinit,
	devshutdown,
	sdioattach,
	sdiowalk,
	sdiostat,
	sdioopen,
	devcreate,
	sdioclose,
	sdioread,
	devbread,
	sdiowrite,
	devbwrite,
	devremove,
	devwstat,
};
