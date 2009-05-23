#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"io.h"

/*
 * first bits of sdio code.  the timing/error checking with commands is still wrong.
 * sdhc cards do not yet work.  normal sd cards might work.  mmc will not work either.
 * using this involves lots of debug prints.
 *
 * http://www.sdcard.org/developers/tech/host_controller/simple_spec/Simplified_SD_Host_Controller_Spec.pdf
 * http://www.sdcard.org/developers/tech/sdcard/pls/Simplified_Physical_Layer_Spec.pdf
 */

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
	int	devsize;
	union {
		struct {
			int	vddrmin, vddrmax, vddwmin, vddwmax;
			int	devsizemult;
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

/* xxx remove global data about inserted card... */
static Cid lastcid;
static int lastcidok;
static Csd lastcsd;
static int lastcsdok;
static int sectorsize = 512;


char Enotimpl[] = "not yet implemented";
char Ecmdfailed[] = "sd command failed";

enum {
	SDOk		= 0,
	SDTimeout	= -1,
	SDCardbusy	= -2,
	SDError		= -3,
};

#define MASK(x)	(((uvlong)1<<(x))-1)
enum {
	/* all registers in the controller are 16 bit */
	Mask16		= (1<<16)-1,

	/* transfer mode */
	TMautocmd12	= 1<<2,
	TMxfertohost	= 1<<4,
	TMswwrite	= 1<<6,

	/* cmd */
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
#define HCtimeout(x)	((x)<<11)
	HCtimeoutenable		= 1<<15,

	/* swreset */
	SRresetall	= 1<<8,

	/* norintrstat */
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
	NSerrorintr	= 1<<15,

	/* errintrstat */
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
	ACMD41powerup		= 1<<31,
};

enum {
	Qdir,
	Qctl,
	Qcid,
	Qcsd,
	Qraw,
	Qdata,
};

static
Dirtab sdiotab[]={
	".",		{Qdir, 0, QTDIR},	0,	0555,
	"sdioctl",	{Qctl, 0},	0,	0666,
	"sdiocid",	{Qcid, 0},	0,	0444,
	"sdiocsd",	{Qcsd, 0},	0,	0444,
	"sdioraw",	{Qraw, 0},	0,	0666,
	"sdiodata",	{Qdata, 0},	0,	0666,
};


/* xxx should make difference between app and non-app commands? */
static ulong
getresptype(ulong cmd)
{
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

static void
delay(void)
{
	volatile int i;
	
	for(i = 0; i < 128*1024; i++)
		{}
}

int
sdcmd0(ulong isapp, ulong cmd, ulong rca, ulong arg, uvlong r[])
{
	SdioReg *reg = SDIOREG;
	int i, s;
	ulong resptype, cmdopts, txmode, v, need;
	uvlong lo, hi;

	print("sdcmd, isapp %lux, cmd %lud, arg %#lux\n", isapp, cmd, arg);

	i = 0;
	for(;;) {
		if((reg->hwstate&(HScmdinhibit|HScardbusy)) == 0)
			break;
		if(i++ >= 100) {
			print("card busy\n");
			return SDCardbusy;
		}
		delay();
	}

	/* "next command is sd application specific" */
	if(isapp && (s = sdcmd0(0, 55, 0, rca<<16, r)) < 0)
		return s;

	/* clear status */
	reg->norintrstat = Mask16&~0;
	reg->errintrstat = Mask16&~0;
	reg->acmd12errstat = Mask16&~0;

	/* prepare args & execute command */
	reg->argcmdlo = Mask16&(arg>>0);
	reg->argcmdhi = Mask16&(arg>>16);
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
	resptype = getresptype(cmd);
	reg->cmd = CMDresp(resptype)|cmdopts|CMDcmd(cmd);
	print("sent: %04lux %04lux %04lux %04lux\n", reg->argcmdlo, reg->argcmdhi, reg->txmode, reg->cmd);

	/* poll for completion/error */
	i = 0;
	need = NScmdcomplete;
	if(cmd == 17 || cmd == 18)
		need |= NSxfercomplete|NSdmaintr;
	for(;;) {
		v = reg->norintrstat;
		if(v&(NSerrorintr|NSunexpresp)) {
			print("cmd error, %04lux\n", reg->errintrstat);
			if(reg->errintrstat&EScmdtimeout)
				return SDTimeout;
			return SDError;
		}
		if((v&need) == need)
			break;
		if(i++ >= 100) {
			print("command unfinished\n");
			return SDError;
		}
		delay();
	}
	print("success, status %04lux %04lux\n", reg->norintrstat, reg->errintrstat);

	print("r %08lux %08lux %08lux %08lux %08lux %08lux %08lux %08lux\n",
		reg->resp[0],
		reg->resp[1],
		reg->resp[2],
		reg->resp[3],
		reg->resp[4],
		reg->resp[5],
		reg->resp[6],
		reg->resp[7]);

	/* fetch the response */
	r[0] = 0;
	r[1] = 0;
	switch(resptype) {
	case CMDrespnone:
		break;
	case CMDresp136:
		/* xxx horrible kludge... will probably change this (and below for 48 bit case) to start the real data at offset 8, to match the sd specs and thus make understanding code easier */
		lo = 0;
		hi = 0;
		lo |= MASK(14)&(uvlong)reg->resp[7]<<0;
		lo |= (uvlong)reg->resp[6]<<(1*16-2);
		lo |= (uvlong)reg->resp[5]<<(2*16-2);
		lo |= (uvlong)reg->resp[4]<<(3*16-2);
		lo |= (uvlong)reg->resp[3]<<(4*16-2);
		hi |= (uvlong)reg->resp[3]>>2;
		hi |= (uvlong)reg->resp[2]<<(1*16-2);
		hi |= (uvlong)reg->resp[1]<<(2*16-2);
		hi |= (uvlong)reg->resp[0]<<(3*16-2);
		r[0] = hi;
		r[1] = lo;
		break;
	case CMDresp48:
	case CMDresp48busy:
		lo = 0;
		lo |= MASK(6)&(uvlong)reg->resp[2];
		lo |= (uvlong)reg->resp[1]<<(0*16+6);
		lo |= (uvlong)reg->resp[0]<<(1*16+6);
		r[0] = 0;
		r[1] = lo;
		break;
	}
	return 0;
}

int
sdcmd(ulong cmd, ulong arg, uvlong r[])
{
	return sdcmd0(0, cmd, 0, arg, r);
}

int
sdacmd(ulong cmd, ulong rca, ulong arg, uvlong r[])
{
	return sdcmd0(1, cmd, rca, arg, r);
}


static void
p32(uchar *p, ulong v)
{
	*p++ = v>>24;
	*p++ = v>>16;
	*p++ = v>>8;
	*p++ = v>>0;
	USED(p);
}

static void
p16(uchar *p, uint v)
{
	*p++ = v>>8;
	*p++ = v>>0;
	USED(p);
}

/*
 * p75 describes the cid register.
 * we don't have the lowest 8 bits, so all offsets are off by 8.
 */
static void
getcid(uvlong r[], Cid *c)
{
	ulong v;

	/* xxx make this use bits() to prevent excessive eye bleed... */

	c->mon = (r[1]>>0)&MASK(4);
	v = (r[1]>>4)&MASK(8);
	c->year = 2000 + 10*(v>>4) + (v&MASK(4));
	/* 4 bits reserved */
	c->serial = (r[1]>>(4+8+4))&MASK(32);
	c->rev = (r[1]>>(4+8+4+32))&MASK(8);

	c->prodname[5] = 0;
	c->prodname[4] = (r[1]>>(4+8+4+32+8))&MASK(8);
	p32((uchar*)c->prodname, (r[0]>>0)&MASK(32));

	c->oid[2] = 0;
	p16((uchar*)c->oid, (r[0]>>32)&MASK(16));

	c->mid = (r[0]>>(32+16))&MASK(8);
}

static char*
cidstr(Cid *c, char *p, int n)
{
	
	snprint(p, n,
		"product %s, rev %#ux, serial %#lux, made %04d-%02d, oem %s, manufacturer %#ux",
		c->prodname,
		c->rev,
		c->serial,
		c->year, c->mon,
		c->oid,
		c->mid);
	return p;
}

static ulong
bits(int msb, int lsb, uvlong v[2])
{
	if(msb <= 63)
		return (v[1]>>lsb)&MASK(msb-lsb+1);
	if(lsb > 63)
		return (v[0]>>(lsb-64))&MASK(msb-lsb+1);
	return ((v[0]&MASK(msb-64+1))<<(63-lsb+1)) | ((v[1]>>lsb)&MASK(63-lsb+1));
}

static int
getcsd(uvlong or[], Csd *c)
{
	uvlong r[2];

	r[0] = (or[0]<<8)|(or[1]>>56);
	r[1] = or[1]<<8;

	print("getcsd, %016llux %016llux\n", r[0], r[1]);

	/* we only know of version 0 and 1 */
	c->version = bits(127, 126, r);
	if(c->version > 1)
		return -1;

	c->taac			= bits(119, 112, r);
	c->nsac			= bits(111, 104, r);
	c->xferspeed		= bits(103, 96, r);
	c->cmdclasses		= bits(95, 84, r);
	c->readblocklength	= bits(83, 80, r);
	c->readblockpartial	= bits(79, 79, r);
	c->writeblockmisalign	= bits(78, 78, r);
	c->readblockmisalign	= bits(77, 77, r);
	c->dsr			= bits(76, 76, r);
	if(c->version == 0) {
		c->devsize	= bits(75, 62, r);
		c->v0.vddrmin	= bits(61, 59, r);
		c->v0.vddrmax	= bits(58, 56, r);
		c->v0.vddwmin	= bits(55, 53, r);
		c->v0.vddwmax	= bits(52, 50, r);
		c->v0.devsizemult	= bits(49, 47, r);
	} else {
		c->devsize	= bits(69, 48, r);
	}
	c->eraseblockenable	= bits(46, 46, r);
	c->erasesectorsize	= bits(45, 39, r);
	c->wpgroupsize		= bits(38, 32, r);
	c->wpgroupenable	= bits(31, 31, r);
	c->writespeedfactor	= bits(28, 26, r);
	c->writeblocklength	= bits(25, 22, r);
	c->writeblockpartial	= bits(21, 21, r);
	c->fileformatgroup	= bits(15, 15, r);
	c->copy			= bits(14, 14, r);
	c->permwriteprotect	= bits(13, 13, r);
	c->tmpwriteprotect	= bits(12, 12, r);
	c->fileformat		= bits(11, 10, r);

	print("fileformat %x, bits %lux, shifted %llux, mask %llux, v %llux\n", c->fileformat, bits(11, 10, r), r[1]>>10, MASK(11-10+1), (r[1]>>10)&MASK(11-10+1));

	return 0;
}

static char*
csdstr(Csd *c, char *p, int n)
{
	char versbuf[128];

	versbuf[0] = '\0';
	if(c->version == 0)
		snprint(versbuf, sizeof versbuf,
			"vddrmin %x\n"
			"vddrmax %x\n"
			"vddwmin %x\n"
			"vddwmax %x\n"
			"devsizemult %x\n",
			c->v0.vddrmin,
			c->v0.vddrmax,
			c->v0.vddwmin,
			c->v0.vddwmax,
			c->v0.devsizemult);
	snprint(p, n,
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
		c->devsize,
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
	return p;
}


/*
 * undoubtly, the timings/polling/error checking is all bogus here, and it's a miracle my sd card works (sometimes).  will clean after another read of the sd specs.
 */
static void
sdinit(void)
{
	uvlong r[2];
	int i;
	int s;
	int sd2, mmc, sdhc;
	Cid cid;
	Csd csd;
	ulong rca, status;
	char *buf;

	/* force card to idle state */
	if(sdcmd(0, 0, r) < 0)
		error("reset failed (cmd0)");

	print("cmd0 done\n");

	/*
	 * "send interface command".  only >=2.00 cards will respond to this.
	 * we send it a check pattern and the voltage range we can do.
	 * if we get a timeout we know the card is not 2.00 or doesn't support the voltage.
	 */
	sd2 = 0;
	s = sdcmd(8, CMD8voltage|CMD8pattern, r);
	if(s == SDOk) {
		sd2 = 1;
		if((r[1]&CMD8patternmask) != CMD8pattern)
			error("sd check pattern mismatch (cmd8)");
		if((r[1]&CMD8voltagemask) != CMD8voltage)
			error("sd voltage not supported (cmd8)");
	} else if(s == SDTimeout)
		sd2 = 0;
	else
		error("sd2 voltage exchange failed (cmd8)");

	print("cmd8 done, sd2 %d\n", sd2);

	/*
	 * "send host capacity support information".
	 * we send the voltages we understand, and that we understand sdhc cards. (xxx but we don't yet!)
	 * mmc cards will not respond.  sd cards will, and tell us if they are sdhc.
	 */
	mmc = 0;
	sdhc = 0;
	for(i = 0;; i++) {
		s = sdacmd(41, 0, ACMD41sdhcsupported|ACMD41voltagewindow, r);
		if(s == SDTimeout) {
			if(sd2)
				error("sd >=2.00 card not responding to acmd41");
			mmc = 1;
			break;
		} else if(s < 0)
			error("exchange voltage/sdhc support info failed (acmd41)");
		if((r[1]&ACMD41voltagewindow) == 0)
			error("voltage not supported (acmd41)");
		if(r[1]&ACMD41powerup) {
			/* xxx on my card the first response to acmd41 does not have the sdhc bit set, the second response does.  if i don't do at least two acmd41's, later commands fail.  the card isn't sdhc afaik... */
			/* xxx perhaps there is a part of the spec that explains that we have to wait for the card to settle down? */
			if(i == 0)
				continue;
			sdhc = (r[1]&ACMD41sdhcsupported) != 0;
			break;
		}

		if(i >= 50)
			error("sd card failed to power up (acmd41)");
	}
	if(mmc)
		error("mmc cards not yet supported"); // xxx p14 says this involves sending cmd1
	print("card, sd2 %d, sdhc %d\n", sd2, sdhc);
	delay();

	print("acmd41 done\n");

	if(sdcmd(2, 0, r) < 0)
		error("send card identification failed (cmd2)");
	getcid(r, &cid);
	lastcid = cid;
	lastcidok = 1;

	buf = malloc(READSTR);
	if(buf == nil)
		error(Enomem);
	print("cmd2 done, cid %s\n", cidstr(&cid, buf, READSTR));
	free(buf);

	if(sdcmd(3, 0, r) < 0)
		error("send relative address failed (cmd3)");
	rca = r[1]>>16;
	status = r[0]&MASK(16);
	print("have card rca %lux, status %lux\n", rca, status);

	if(sdcmd(9, rca<<16, r) < 0)
		error("send csd failed (cmd9)");
	getcsd(r, &csd);
	lastcsd = csd;
	lastcsdok = 1;

	buf = malloc(READSTR);
	if(buf == nil)
		error(Enomem);
	print("cmd9 done\n");
	print("%s", csdstr(&csd, buf, READSTR));
	free(buf);

	if(csd.version == 0) {
		sectorsize = 1<<csd.readblocklength;
		print("csd v0, block length read/write %d/%d, size %,d bytes, eraseblock %d",
			1<<csd.readblocklength, 
			1<<csd.writeblocklength,
			(csd.devsize+1)*(1<<(csd.v0.devsizemult+2))*(1<<csd.readblocklength),
			(1<<csd.writeblocklength)*(csd.erasesectorsize+1));
	} else {
		sectorsize = 512;
		print("csd v1, fixed 512 block length, size %,d bytes, eraseblock fixed 512",
			(csd.devsize+1)*512*1024);
	}


	if(sdcmd(7, rca<<16, r) < 0)
		error("card select failed (cmd7)");

	print("card selected\n");

	if(sdacmd(6, rca, (1<<1), r) < 0)
		error("set buswidth to 4-bit failed (acmd6)");

	print("talking in 4-bit width\n");

	/* this is mandatory.  sd spec says this cannot be done if reads can't be partial, but that's probably wrong. */
	if(sdcmd(16, 512, r) < 0)
		error("set block length failed (cmd16)");
	print("block length set\n");
}


static long
sdio(uchar *a, long n, vlong offset, int iswrite)
{
	uvlong r[2];
	SdioReg *reg = SDIOREG;
	uchar *buf;

	if(iswrite)
		error(Enotimpl);

	/* xxx we should cover this cases with a buffer, and then use the same code to allow non-sector-aligned reads? */
	if((ulong)a % 4 != 0)
		error("bad buffer alignment...");

	if(offset % 512 != 0)
		error("not sector aligned");
	if(n % 512 != 0)
		error("not multiple of sector size");

	/* xxx for some reason i couldn't discover, using "a" directly causes corruption and makes the alloc routines panic in freetype/freeptrs...  i've check with a larger buffer, there doesn't seemt to be corruption before/after the buffer... */
	buf = malloc(n);
	if(buf == nil)
		error(Enomem);
	
	if(waserror()) {
		free(buf);
		nexterror();
	}

	reg->dmaaddrlo = (ulong)buf&Mask16;
	reg->dmaaddrhi = ((ulong)buf>>16)&Mask16;
	reg->blksize = 512;
	reg->blkcount = n/512;
	if(sdcmd(18, (ulong)offset, r) < 0)
		error("read failed (cmd18)");
	memmove(a, buf, n);

	free(buf);
	poperror();

	return n;
}

static void
sdioreset(void)
{
	SdioReg *reg = SDIOREG;

	print("sdioreset\n");

	/* configure host controller */
	reg->hostctl = HCpushpull|HCcardtypememonly|HCbigendian|HCdatawidth4|HCtimeout(15); // xxx HCtimeoutenable;

	/* reset the bus, forcing all cards to idle state */
	reg->swreset = SRresetall;

	/* disable interrupts */
	reg->norintrena = 0;
	reg->errintrena = 0;

	/* enable all status reporting */
	reg->norintrstatena = Mask16&~0;
	reg->errintrstatena = Mask16&~0;
}

static void
sdioinit(void)
{
	print("sdioinit\n");
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
	c->aux = nil;
	return devopen(c, omode, sdiotab, nelem(sdiotab), devgen);
}

static void
sdioclose(Chan* c)
{
	free(c->aux);
	c->aux = nil;
}

static long
sdioread(Chan* c, void* a, long n, vlong offset)
{
	SdioReg *reg = SDIOREG;
	char *buf;

	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, sdiotab, nelem(sdiotab), devgen);
	case Qctl:
		buf = malloc(READSTR);
		if(buf == nil)
			error(Enomem);
		snprint(buf, READSTR,
			"resp0-3 %04lux %04lux %04lux %04lux\n"
			"resp5-7 %04lux %04lux %04lux %04lux\n"
			"hwstate %04lux\n"
			"hostctl %04lux\n"
			"normal  %04lux\n"
			"error   %04lux\n",
			reg->resp[0], reg->resp[1], reg->resp[2], reg->resp[3],
			reg->resp[4], reg->resp[5], reg->resp[6], reg->resp[7],
			reg->hwstate,
			reg->hostctl,
			reg->norintrstat,
			reg->errintrstat);
		n = readstr(offset, a, n, buf);
		free(buf);
		break;
	case Qcid:
		if(lastcidok == 0)
			error("no cid read yet");
		buf = malloc(READSTR);
		if(buf == nil)
			error(Enomem);
		n = readstr(offset, a, n, cidstr(&lastcid, buf, READSTR));
		free(buf);
		break;
	case Qcsd:
		if(lastcsdok == 0)
			error("no csd read yet");
		buf = malloc(READSTR);
		if(buf == nil)
			error(Enomem);
		n = readstr(offset, a, n, csdstr(&lastcsd, buf, READSTR));
		free(buf);
		break;
	case Qraw:
		if(c->aux == nil)
			error("no command executed");
		n = readstr(offset, a, n, c->aux);
		break;
	case Qdata:
		n = sdio(a, n, offset, 0);
		break;
	default:
		n = 0;
		break;
	}
	return n;
}

static long
sdiowrite(Chan* c, void* a, long n, vlong offset)
{
	char buf[128];
	char *p, *e;
	ulong isapp, cmd, arg;
	uvlong resp[2];
	SdioReg *reg = SDIOREG;

	USED(offset);

	switch((ulong)c->qid.path){
	case Qctl:
		if(n >= sizeof buf)
			error(Etoobig);
		memmove(buf, a, n);
		buf[n] = 0;
		if(strcmp(buf, "reset") == 0 || strcmp(buf, "reset\n") == 0) {
			reg->swreset = SRresetall;
			delay();
		} else if(strcmp(buf, "init") == 0 || strcmp(buf, "init\n") == 0) {
			sdinit();
		} else {
			print("bad ctl: %q\n", buf);
			error(Ebadctl);
		}
		break;
	case Qraw:
		/* line with:  (a)cmdX, arg (in hex).  result is the response in hex. */
		if(n >= sizeof buf)
			error(Etoobig);
		memmove(buf, a, n);
		buf[n] = 0;
		p = buf;
		e = buf+n;
		isapp = 0;
		if(p < e && *p == 'a') {
			isapp = 1;
			p++;
		}
		if(e-p < 3 || strncmp(p, "cmd", 3) != 0) {
			print("not cmd\n");
			error(Ebadarg);
		}
		p += 3;
		cmd = strtoul(p, &p, 10);
		if(*p != ' ') {
			print("no space after cmd num\n");
			error(Ebadarg);
		}
		p++;
		arg = strtoul(p, &p, 0);
		if(strcmp(p, "") != 0 && strcmp(p, "\n") != 0) {
			print("no arg after space, p %p e %p, p %q\n", p, e, p);
			error(Ebadarg);
		}
		if(sdcmd0(isapp, cmd, 0, arg, resp) < 0)
			error(Ecmdfailed);
		free(c->aux);
		c->aux = smprint("result: %016llux %016llux\n", resp[0], resp[1]);
		if(c->aux == nil)
			error(Enomem);
		break;
	case Qdata:
		n = sdio(a, n, offset, 0);
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
