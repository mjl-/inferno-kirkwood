/*
 * only memory cards are supported.  only sd cards for now, mmc cards seem obsolete anyway.
 *
 * todo:
 * - abort command on error during sleep.
 * - make sdio() split transaction when it is huge.
 * - better error handling
 * - hook into devsd.c?
 * - use interrupts for all commands?  should be better than polling.
 * - see if we can detect device inserts/ejects?  yes, by sd_cd gpio pin (on mpp47).
 * - test with non-high capacity sd cards.  i think it does need different code.
 * - ctl commands for erasing?
 * - erase before writing big buffer?
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"io.h"
#include	"sdcard.h"

#define DEBUG 1
#define dprint	if(DEBUG)print

char Enocard[] = "no card";

enum {
	SDOk		= 0,
	SDTimeout	= -1,
	SDCardbusy	= -2,
	SDError		= -3,

	/* expected response to command */
	R0	= 0,
	R1,
	R1b,
	R2,
	R3,
	R6,
	R7,

	/* flags for cmd and cmddma */
	Fapp	= 1<<0,		/* app-specific command */
	Fappdata= 1<<1,		/* app-specific data command */
	Fdmad2h	= 1<<2,		/* dma from device to host */
	Fdmah2d	= 1<<3,		/* dma from host to device */
	Fmulti	= 1<<4,		/* multiple blocks, use auto cmd 12 to stop transfer */
};

enum {
	/* transfer mode */
	/* note: the doc bits is contradictory on these bits.  comments below indicate actual behaviour. */
	TMdatawrite	= 1<<1,		/* write data after response, for write commands */
	TMautocmd12	= 1<<2,		/* hardware sends cmd12 */
	TMxfertohost	= 1<<4,		/* data flows from sd to host */
	TMswwrite	= 1<<6,		/* software-controlled, not dma */

	/* cmd */
	Respnone	= 0<<0,
	Resp136		= 1<<0,
	Resp48		= 2<<0,
	Resp48busy	= 3<<0,

	CMDdatacrccheck	= 1<<2,
	CMDcmdcrccheck	= 1<<3,
	CMDcmdindexcheck= 1<<4,
	CMDdatapresent	= 1<<5,
	CMDunexpresp	= 1<<7,
	CMDshift	= 8,

	/* hoststate */
	HScmdinhibit	= 1<<0,
	HScardbusy	= 1<<1,
	HStxactive	= 1<<8,
	HSrxactive	= 1<<9,
	HSfifofull	= 1<<12,
	HSfifoempty	= 1<<13,
	HSacmd12active	= 1<<14,

	/* hostctl */
	HCpushpull	= 1<<0,
	HCcardmask	= 3<<1,
	HCcardmemonly	= 0<<1,
	HCcardioonly	= 1<<1,
	HCcardiomem	= 2<<1,
	HCcardmmc	= 3<<1,
	HCbigendian	= 1<<3,
	HClsbfirst	= 1<<4,
	HCdatawidth4	= 1<<9,
	HChighspeed	= 1<<10,
#define HCtimeout(x)	(((x) & MASK(4))<<11)
	HCtimeoutenable	= 1<<15,

	/* swreset */
	SRresetall	= 1<<8,

	/* st, status */
	Scmdcomplete	= 1<<0,
	Sxfercomplete	= 1<<1,
	Sblockgapevent	= 1<<2,
	Sdmaintr	= 1<<3,
	Stxready	= 1<<4,
	Srxready	= 1<<5,
	Scardintr	= 1<<8,
	Sreadwaiton	= 1<<9,
	Sfifo8wfull	= 1<<10,
	Sfifo8wavail	= 1<<11,
	Ssuspended	= 1<<12,
	Sautocmd12done	= 1<<13,
	Sunexpresp	= 1<<14,
	Serror		= 1<<15,

	/* est, error status */
	Ecmdtimeout	= 1<<0,
	Ecmdcrc	= 1<<1,
	Ecmdendbit	= 1<<2,
	Ecmdindex	= 1<<3,
	Edatatimeout	= 1<<4,
	Erddatacrc	= 1<<5,
	Erddataend	= 1<<6,
	Eautocmd12	= 1<<8,
	Ecmdstartbit	= 1<<9,
	Exfersize	= 1<<10,
	Eresptbit	= 1<<11,
	Ecrcendbit	= 1<<12,
	Ecrcstartbit	= 1<<13,
	Ecrcstatus	= 1<<14,

	/* autocmd12 index */
	AIcheckbusy	= 1<<0,
	AIcheckindex	= 1<<1,
	AIcmdshift	= 8,
};

enum {
	/* sd commands */
	CMDRead		= 17,
	CMDReadmulti	= 18,
	CMDWrite	= 24,
	CMDWritemulti	= 25,

	ACMDGetscr	= 51,
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


static Card card;
static Rendez dmar;

enum {
	Qdir, Qctl, Qinfo, Qdata, Qstatus,
};
static
Dirtab sdiotab[] = {
	".",		{Qdir, 0, QTDIR},	0,	0555,
	"sdioctl",	{Qctl},			0,	0666,
	"sdioinfo",	{Qinfo},		0,	0444,
	"sdio",		{Qdata},		0,	0666,
	"sdiostatus",	{Qstatus},		0,	0444,
};


/* for bits in st, est, acmd12st registers */
static char *statusstrs[] = {
"cmdcomplete", "xfercomplete", "blockgapevent", "dmaintr",
"txready", "rxready", "", "",
"cardintr", "readwaiton", "fifo8wfull", "fifo8wavail",
"suspended", "autocmd12done", "unexpresp", "errorintr",
};

static char *errstatusstrs[] = {
"cmdtimeout", "cmdcrc", "cmdendbit", "cmdindex",
"datatimeout", "rddatacrc", "rddataend", "",
"autocmd12", "cmdstartbit", "xfersize", "resptbit",
"crcendbit", "crcstartbit", "crcstatus",
};

static char *acmd12ststrs[] = {
"acmd12notexe", "acmd12timeout", "acmd12crcerr", "acmd12endbiterr",
"acmd12indexerr", "acmd12resptbi", "acmd12respstartbiterr",
};

static char*
mkstr(char *p, char *e, ulong v, char **s, int ns)
{
	int i;
	for(i = 0; i < ns; i++)
		if(v & (1<<i))
			p = seprint(p, e, " %q", s[i]);
	return p;
}

static char*
statusstr(char *p, char *e, ulong v)
{
	return mkstr(p, e, v, statusstrs, nelem(statusstrs));
}

static char*
errstatusstr(char *p, char *e, ulong v)
{
	return mkstr(p, e, v, errstatusstrs, nelem(errstatusstrs));
}


static char*
acmd12ststr(char *p, char *e, ulong v)
{
	return mkstr(p, e, v, acmd12ststrs, nelem(acmd12ststrs));
}

static void
printstatus(char *s, ulong st, ulong est, ulong acmd12st)
{
	char *p, *e;

	if(!DEBUG)
		return;

	p = up->genbuf;
	e = p+sizeof (up->genbuf);

	p = statusstr(p, e, st);
	p = seprint(p, e, ";");
	p = errstatusstr(p, e, est);
	p = seprint(p, e, ";");
	p = acmd12ststr(p, e, acmd12st);
	USED(p);

	print("status: %s%s\n", s, up->genbuf);
}


static char *errstrs[] = {
"success", "timeout", "card busy", "error",
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
dmadone(void*)
{
	SdioReg *r = SDIOREG;
	ulong need = Scmdcomplete|Sdmaintr;

	if((r->st & need) == need) {
		dprint("dmadone\n");
		return 1;
	}
	dprint("not dmadone\n");
	r->stirq = Sdmaintr|Serror|Sunexpresp;
	r->estirq = ~0;
	return 0;
}

/* keep in sync with R* contants */
static ulong resptypes[] = {
Respnone, Resp48, Resp48busy, Resp136, Resp48, Resp48, Resp48,
};
static int
sdcmd(Card *c, ulong cmd, ulong arg, int rt, ulong fl)
{
	SdioReg *r = SDIOREG;
	int i, s;
	ulong acmd;
	ulong cmdopts, mode;
	ulong need, v;
	uvlong w;

	i = 0;
	for(;;) {
		if((r->hoststate & (HScmdinhibit|HScardbusy)) == 0)
			break;
		if(i++ >= 50)
			return SDCardbusy;
		tsleep(&up->sleep, return0, nil, 10);
	}

	if(fl & (Fapp|Fappdata)) {
		acmd = 55;
		if(fl & Fappdata)
			acmd = 56;
		s = sdcmd(c, acmd, card.rca<<16, R1, 0);
		if(s < 0)
			return s;
	}

	/* clear status */
	r->st = ~0;
	r->est = ~0;
	r->acmd12st = ~0;
	printstatus("before cmd: ", r->st, r->est, r->acmd12st);

	microdelay(1000);
	/* prepare args & execute command */
	r->arglo = (arg>>0) & MASK(16);
	r->arghi = (arg>>16) & MASK(16);
	cmdopts = CMDunexpresp;
	mode = 0;
	if(fl & Fdmad2h)
		mode |= TMxfertohost;
	if(fl & Fdmah2d)
		mode |= TMdatawrite;
	if(fl & Fmulti) {
		mode |= TMautocmd12;
		r->acmd12arglo = 0;
		r->acmd12arghi = 0;
		r->acmd12idx = AIcheckbusy|AIcheckindex|(12<<AIcmdshift);
	}
	if(fl & (Fdmad2h|Fdmah2d))
		cmdopts |= CMDdatapresent;
	if(fl & Fdmad2h)
		cmdopts |= CMDdatacrccheck;
	if(rt != R0 && rt != R3)
		cmdopts |= CMDcmdcrccheck;
	if(rt != R0 && rt != R2 && rt != R3)
		cmdopts |= CMDcmdindexcheck;
	r->mode = mode;
	r->cmd = (cmd<<CMDshift)|cmdopts|resptypes[rt];

	if(fl & (Fdmad2h|Fdmah2d)) {
		/* wait for dma interrupt that signals completion */
		while(waserror())
			{}
		tsleep(&dmar, dmadone, nil, 5000);
		poperror();

		need = Scmdcomplete|Sdmaintr;
		if((r->st & need) != need || (r->st & (Serror|Sunexpresp))) {
			printstatus("dma error: ", r->st, r->est, r->acmd12st);
			return SDError;
		}
	} else {
		/* poll for completion/error */
		need = Scmdcomplete;
		i = 0;
		for(;;) {
			v = r->st;
			if(v & (Serror|Sunexpresp)) {
				printstatus("error: ", v, r->est, r->acmd12st);
				if(r->est & Ecmdtimeout)
					return SDTimeout;
				return SDError;
			}
			if((v & need) == need)
				break;
			if(i++ >= 100) {
				print("command unfinished\n");
				printstatus("timeout: ", v, r->est, r->acmd12st);
				return SDError;
			}
			tsleep(&up->sleep, return0, nil, 10);
		}
	}
	printstatus("success", r->st, r->est, r->acmd12st);

	/* fetch the response */
	memset(c->resp, 0, sizeof c->resp);
	switch(resptypes[rt]) {
	case Respnone:
		break;
	case Resp136:
		c->resp[0] = (uvlong)r->resp[0]>>10;

		w = 0;
		w |= (uvlong)r->resp[4]>>10;
		w |= (uvlong)r->resp[3]<<(70-64);
		w |= (uvlong)r->resp[2]<<(86-64);
		w |= (uvlong)r->resp[1]<<(102-64);
		w |= (uvlong)r->resp[0]<<(118-64);
		c->resp[1] = w;

		w = r->crc7<<1;
		w |= ((uvlong)r->resp[7] & MASK(14))<<8;
		w |= (uvlong)r->resp[6]<<22;
		w |= (uvlong)r->resp[5]<<38;
		w |= (uvlong)r->resp[4]<<54;
		c->resp[2] = w;

		break;
	case Resp48:
	case Resp48busy:
		w = r->crc7<<1;
		w |= ((uvlong)r->resp[2] & MASK(6))<<8;
		w |= (uvlong)r->resp[1]<<14;
		w |= (uvlong)r->resp[0]<<30;
		c->resp[0] = 0;
		c->resp[1] = 0;
		c->resp[2] = w;
		break;
	}
	return 0;
}

static int
sdcmddma(Card *c, ulong cmd, ulong arg, void *a, int blsz, int nbl, int resp, ulong fl)
{
	SdioReg *r = SDIOREG;

	dcwbinv(a, blsz*nbl);
	r->dmaaddrlo = (ulong)a & MASK(16);
	r->dmaaddrhi = ((ulong)a>>16) & MASK(16);
	r->blksize = blsz;
	r->blkcount = nbl;
	//tsleep(&up->sleep, return0, nil, 250);
	dprint("sdio, a %#lux, dmaddrlo %#lux dmaadrhi %#lux blksize %d blkcount %d cmdarg %#lux\n",
		a, (ulong)a & MASK(16), ((ulong)a>>16) & MASK(16), blsz, nbl, arg);
	//tsleep(&up->sleep, return0, nil, 250);
	return sdcmd(c, cmd, arg, resp, fl);
}

static void
sdclock(ulong v)
{
	SDIOREG->clockdiv = ((100*1000*1000)/v)-1;
}

static void
sdinit(void)
{
	int i, s;
	ulong v;
	SdioReg *r = SDIOREG;

	sdclock(400*1000);
	r->hostctl &= ~HChighspeed;

	/* force card to idle state */
	if(sdcmd(&card, 0, 0, R0, 0) < 0)
		errorsd("reset failed");

	/*
	 * "send interface command".  only >=2.00 cards will respond.
	 * we send a check pattern and supported voltage range.
	 */
	card.mmc = 0;
	card.sd2 = 0;
	card.sdhc = 0;
	card.rca = 0;
	s = sdcmd(&card, 8, CMD8voltage|CMD8pattern, R7, 0);
	if(s == SDOk) {
		card.sd2 = 1;
		v = card.resp[2]>>8;
		if((v & CMD8patternmask) != CMD8pattern)
			errorsd("sd check pattern mismatch");
		if((v & CMD8voltagemask) != CMD8voltage) {
			errorsd("sd voltage not supported");
		}
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
		s = sdcmd(&card, 41, v, R3, Fapp);
		if(s == SDTimeout) {
			if(card.sd2)
				errorsd("sd >=2.00 card");
			card.mmc = 1;
			break;
		}
		if(s < 0)
			errorsd("exchange voltage/sdhc support info");
		v = card.resp[2]>>8;
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

	if(sdcmd(&card, 2, 0, R2, 0) < 0)
		errorsd("card identification");
	if(parsecid(&card.cid, card.resp) < 0)
		errorsd("bad cid register");

	i = 0;
	for(;;) {
		if(sdcmd(&card, 3, 0, R6, 0) < 0)
			errorsd("getting relative address");
		card.rca = (card.resp[2]>>24) & MASK(16);
		v = (card.resp[2]>>8) & MASK(16);
		dprint("have card rca %ux, status %lux\n", card.rca, v);
		USED(v);
		if(card.rca != 0)
			break;
		if(i++ == 10)
			errorsd("card insists on invalid rca 0");
	}

	if(sdcmd(&card, 9, card.rca<<16, R2, 0) < 0)
		errorsd("get csd");
	if(parsecsd(&card.csd, card.resp) < 0)
		errorsd("bad csd register");

	if(card.csd.version == 0) {
		card.bs = 1<<card.csd.readblocklength;
		card.size = card.csd.size+1;
		card.size *= 1<<(card.csd.v0.sizemult+2);
		card.size *= 1<<card.csd.readblocklength;
		print("csd0, block length read/write %d/%d, size %lld bytes, eraseblock %d\n",
			1<<card.csd.readblocklength, 
			1<<card.csd.writeblocklength,
			card.size,
			(1<<card.csd.writeblocklength)*(card.csd.erasesectorsize+1));
	} else {
		card.bs = 512;
		card.size = (vlong)(card.csd.size+1)*card.bs*1024;
		print("csd1, fixed 512 block length, size %lld bytes, eraseblock fixed 512\n", card.size);
	}

	if(card.sdhc) {
		dprint("enabling sdhc & setting clock to 50mhz\n");
		sdclock(50*1000*1000);
		r->hostctl |= HChighspeed;
	} else {
		dprint("leaving sdhc off & setting clock to 25mhz\n");
		sdclock(25*1000*1000);
	}

	if(sdcmd(&card, 7, card.rca<<16, R1b, 0) < 0)
		errorsd("selecting card");

if(0){	
	uchar *p = malloc(512);
	if(sdcmddma(&card, 55, 1<<0, p, 512, 1, R1, Fappdata|Fdmad2h) < 0)
		errorsd("read scr");
}

	/* xxx have to check if this is supported by card.  in scr register */
	if(sdcmd(&card, 6, (1<<1), R1, Fapp) < 0)
		errorsd("setting buswidth to 4-bit");

	if(sdcmd(&card, 16, card.bs, R1, 0) < 0)
		errorsd("setting block length");

	sdiotab[Qdata].length = card.size;
	card.valid = 1;
	print("%s", cardstr(&card, up->genbuf, sizeof (up->genbuf)));
}


static long
sdio(uchar *a, long n, vlong offset, int iswrite)
{
	ulong cmd, arg, fl;

	if(card.valid == 0)
		error(Enocard);

	/* xxx we should cover this cases with a buffer, and then use the same code to allow non-sector-aligned reads? */
	if((ulong)a % 4 != 0)
		error("bad buffer alignment...");

	if(offset % card.bs != 0)
		error("not sector aligned");
	if(n % card.bs != 0)
		error("not multiple of sector size");

	cmd = iswrite ? CMDWritemulti : CMDReadmulti;
	fl = iswrite ? Fdmah2d : Fdmad2h;
	if(card.sdhc)
		arg = offset/card.bs;
	else
		arg = offset;
	if(sdcmddma(&card, cmd, arg, a, card.bs, n/card.bs, R1, fl|Fmulti) < 0)
		errorsd("io");
	return n;
}

static void
sdiointr(Ureg*, void*)
{
	SdioReg *r = SDIOREG;
	char buf[128];
	char *p, *e;

	if(DEBUG) {
		iprint("sdio intr %lux %lux %lux %lux\n",
			r->st,
			r->st & r->stirq,
			r->est,
			r->est & r->estirq);

		p = buf;
		e = p+sizeof (buf);

		p = statusstr(p, e, r->st);
		p = seprint(p, e, ";");
		p = errstatusstr(p, e, r->est);
		USED(p);

		iprint("intr: %s\n", buf);
	}

	/*
	 * for now, interrupts are only used for dma transfers.
	 * don't clear the status, just make sure we are not called again
	 * before this interrupt is handled.
	 */
	wakeup(&dmar);
	r->stirq &= ~Sdmaintr;
	intrclear(Irqlo, IRQ0sdio);
}

static void
sdioreset(void)
{
	SdioReg *r = SDIOREG;

	/* disable all interrupts.  dma interrupt will be enabled as required.  all bits lead to IRQ0sdio. */
	r->stirq = 0;
	r->estirq = 0;
	intrenable(Irqlo, IRQ0sdio, sdiointr, nil, "sdio");
}

static void
sdioinit(void)
{
	SdioReg *r = SDIOREG;

	card.valid = 0;

	/* reset the bus, forcing all cards to idle state */
	r->swreset = SRresetall;
	tsleep(&up->sleep, return0, nil, 50);

	sdclock(25*1000*1000);

	/* configure host controller */
	r->hostctl = HCpushpull|HCcardmemonly|HCbigendian|HCdatawidth4|HCtimeout(15)|HCtimeoutenable;

	/* clear status */
	r->st = ~0;
	r->est = ~0;

	/* enable most status reporting */
	r->stena = ~(Stxready|Sfifo8wavail);
	r->estena = ~0;

	/* disable all interrupts.  dma interrupt will be enabled as required.  all bits lead to IRQ0sdio. */
	r->stirq = 0;
	r->estirq = 0;
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
	SdioReg *r = SDIOREG;

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
		break;
	case Qstatus:
		p = buf = smalloc(READSTR);
		e = p+READSTR;

		p = seprint(p, e, "st:       ");
		p = statusstr(p, e, r->st);
		p = seprint(p, e, "\nest:      ");
		p = errstatusstr(p, e, r->est);
		p = seprint(p, e, "\nacmd12st: ");
		p = acmd12ststr(p, e, r->acmd12st);

		p = seprint(p, e, "\nstena:    ");
		p = statusstr(p, e, r->stena);
		p = seprint(p, e, "\nestena:   ");
		p = errstatusstr(p, e, r->estena);

		p = seprint(p, e, "\nstirq:    ");
		p = statusstr(p, e, r->stirq);
		p = seprint(p, e, "\nestirq:   ");
		p = errstatusstr(p, e, r->estirq);

		p = seprint(p, e, "\nhoststate %#lux\n", r->hoststate);
		USED(p);

		n = readstr(offset, a, n, buf);
		free(buf);
		break;
	default:
		n = 0;
		break;
	}
	return n;
}

enum {
	CMreset, CMinit, CMclean,
};
static Cmdtab sdioctl[] = 
{
	CMreset,	"reset",	1,
	CMinit,		"init",		1,
	CMclean,	"clean",	1,
};

static long
sdiowrite(Chan* c, void* a, long n, vlong offset)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	SdioReg *r = SDIOREG;

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
		case CMclean:
			r->st = ~0;
			r->est = ~0;
			r->acmd12st = ~0;
			break;
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
