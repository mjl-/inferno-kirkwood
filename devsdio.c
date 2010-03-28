/*
 * only memory cards are supported (not sdio).  only sd cards for now, mmc cards seem obsolete anyway.
 *
 * todo:
 * - interrupts are sometimes lost?
 * - don't crash when proc doing read/write is killed.
 * - hook into devsd.c?
 * - see if we can detect device inserts/ejects?  yes, by sd_cd gpio pin (on mpp47).
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
	/* keep in sync with errstrs[] */
	SDOk		= 0,
	SDTimeout	= -1,
	SDCardbusy	= -2,
	SDError		= -3,
	SDInterrupted	= -4,
	SDBadstatus	= -5,	/* status in r1(b) response indicated error, see card.status */

	/* expected response to command */
	R0	= 0,
	R1,
	R1b,
	R2,
	R3,
	R6,
	R7,

	/* flags for sdcmd and sdcmddma */
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
	Ecmdcrc		= 1<<1,
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


	CMD8pattern	= 0xaa,
	CMD8patternmask	= MASK(8),
	CMD8voltage	= 1<<8,
	CMD8voltagemask	= MASK(4)<<8,

	ACMD41voltagewindow	= MASK(23-15+1)<<15,
	ACMD41sdhcsupported	= 1<<30,
	ACMD41ready		= 1<<31,


	/* sd status, in R1 & R1b responses */
	SDappcmd	= 1<<5,		/* next command will be interpreted as app specific */
	SDreadyfordata	= 1<<8,		/* buffer empty signal on bus */
	SDstateshift	= 9,
	SDstatewidth	= 4,
	SDnoecc		= 1<<14,	/* command executed without ecc */
	SDerror		= 1<<19,	/* general/unknown error */
	SDccerror	= 1<<20,	/* internal card controller error */
	SDbadcmd	= 1<<22,	/* invalid command */
	SDblocklenerr	= 1<<29,	/* block length invalid */

	/* bits we consider as error.  some indicate conditions we don't (yet) handle. */
	SDbad		= 1<<3|1<<13|1<<14|1<<15|1<<16|MASK(13)<<19,
};


static Card card;
static Rendez cmdr;
static QLock sdl;

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
static const char *statusstrs[] = {
"cmdcomplete", "xfercomplete", "blockgapevent", "dmaintr",
"txready", "rxready", "", "",
"cardintr", "readwaiton", "fifo8wfull", "fifo8wavail",
"suspended", "autocmd12done", "unexpresp", "errorintr",
};

static const char *errstatusstrs[] = {
"cmdtimeout", "cmdcrc", "cmdendbit", "cmdindex",
"datatimeout", "rddatacrc", "rddataend", "",
"autocmd12", "cmdstartbit", "xfersize", "resptbit",
"crcendbit", "crcstartbit", "crcstatus",
};

static const char *acmd12ststrs[] = {
"acmd12notexe", "acmd12timeout", "acmd12crcerr", "acmd12endbiterr",
"acmd12indexerr", "acmd12resptbi", "acmd12respstartbiterr",
};

static const char *sdstatusstrs[] = {
"tm0", "tm1", "apprsvd", "akeseqerr",
"sdiorsvd", "appcmd", "rsvd6", "rsvd7",
"readyfordata", "st9", "st10", "st11",
"st12", "erasereset", "noecc", "wperaseskip",
"csdoverwrite", "rsvd17", "rsvd18", "error",
"ccerror", "eccfail", "badcmd", "cmdcrcerr",
"lockerr", "locked", "wpviolation", "eraseparam",
"eraseseqerr", "blocklenerr", "addrerr", "outofrange",
};

static char*
mkstr(char *p, char *e, ulong v, char **s, int ns)
{
	int i;
	for(i = 0; i < ns; i++)
		if(v & 1<<i)
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

static char *
sdstatusstr(char *p, char *e, ulong v)
{
	return mkstr(p, e, v, sdstatusstrs, nelem(sdstatusstrs));
}

static const char *statestrs[] = {
"idle", "ready", "ident", "stby",
"tran", "data", "rcv", "prg",
"dis", "", "", "",
"", "", "", "",
};

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


static const char *errstrs[] = {
"success", "timeout", "card busy", "error", "interrupted", "badstatus",
};

static void
errorsd(char *s, int v)
{
	char buf[ERRMAX];
	char * p = buf;
	char * const e = buf+sizeof buf;

	p = seprint(p, e, "%s", s);
	if(v != SDOk)
		p = seprint(p, e, ": %s", errstrs[-v]);
	if(v == SDBadstatus)
		p = seprint(p, e, " (r1status %#lux)", card.status);
	if(v == SDError) {
		p = seprint(p, e, " (est %#lux", card.status);
		p = errstatusstr(p, e, card.status);
		p = seprint(p, e, ")");
	}
	USED(p);
	error(buf);
}

static int
isdone(void *)
{
	SdioReg *r = SDIOREG;

	if(r->st & r->stirq) {
		r->stirq = 0;
		return 1;
	}
	return 0;
}

/* keep in sync with R* contants */
static const ulong resptypes[] = {
Respnone, Resp48, Resp48busy, Resp136, Resp48, Resp48, Resp48,
};
static int
sdcmd(Card *c, ulong cmd, ulong arg, int rt, ulong fl)
{
	SdioReg *r = SDIOREG;
	int i, s;
	ulong acmd;
	ulong cmdopts, mode;
	ulong need;
	uvlong w;

	print("sdcmd, cmd %lud, arg %lud, fl %#lux\n", cmd, arg, fl);

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
		if((c->resp[2]>>8 & SDappcmd) == 0)
			return SDBadstatus;
	}

	/* clear status */
	r->st = ~0;
	r->est = ~0;
	r->acmd12st = ~0;
	// xxx remove?
	printstatus("before cmd: ", r->st, r->est, r->acmd12st);

	// xxx?
	microdelay(1000);

	/* prepare args & execute command */
	r->arglo = arg>>0 & MASK(16);
	r->arghi = arg>>16 & MASK(16);
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
		r->acmd12idx = AIcheckbusy|AIcheckindex|12<<AIcmdshift;
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
	r->cmd = cmd<<CMDshift|cmdopts|resptypes[rt];

	/* Scmdcomplete/Sdma and errors signal completion */
	s = 0;
	need = Scmdcomplete;
	if(fl & (Fdmad2h|Fdmah2d))
		need = Sdmaintr;
	r->stirq = need|Sunexpresp|Serror;
	r->estirq = ~0;
	while(waserror())
		s = SDInterrupted;
	tsleep(&cmdr, isdone, nil, 5000);
	poperror();

	if(s == SDInterrupted)
		return s;
	if(r->st & (Sunexpresp|Serror) || (r->st & need) == 0) {
		card.status = r->est;
		return SDError;
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

	if(rt == R1 || rt == R1b) {
		c->status = c->resp[2]>>8;
		if(c->status & SDbad)
			return SDBadstatus;
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
	dprint("sdcmddma, a %#lux, dmaddrlo %#lux dmaadrhi %#lux blksize %d blkcount %d cmdarg %#lux\n",
		a, (ulong)a & MASK(16), ((ulong)a>>16) & MASK(16), blsz, nbl, arg);
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
	s = sdcmd(&card, 0, 0, R0, 0);
	if(s < 0)
		errorsd("reset failed", s);

	/*
	 * "send interface command".  only >=2.00 cards will respond.
	 * we send a check pattern and supported voltage range.
	 */
	card.mmc = 0;
	card.sd2 = 0;
	card.sdhc = 0;
	card.rca = 0;
	s = sdcmd(&card, 8, CMD8voltage|CMD8pattern, R7, 0);
	switch(s) {
	case SDOk:
		card.sd2 = 1;
		v = card.resp[2]>>8;
		if((v & CMD8patternmask) != CMD8pattern)
			error("check pattern mismatch");
		if((v & CMD8voltagemask) != CMD8voltage)
			error("voltage not supported");
		break;

	case SDTimeout:
	case SDError:	/* "no response" from spec can result in error too apparently */
		/* sd 1.x or not an sd memory card */
		s = sdcmd(&card, 0, 0, R0, 0);
		if(s < 0)
			errorsd("reset failed", s);
		break;
	default:
		errorsd("voltage exchange failed", s);
	}

	/*
	 * "send host capacity support information".
	 * we send supported voltages & our sdhc support.
	 * mmc cards won't respond.  sd cards will power up and indicate
	 * if they support sdhc.
	 */
	i = 0;
	for(;;) {
		v = ACMD41voltagewindow;
		if(card.sd2)
			v |= ACMD41sdhcsupported;
		s = sdcmd(&card, 41, v, R3, Fapp);
		if(s < 0) {
			if(s == SDTimeout && !card.sd2)
				card.mmc = 1;
			errorsd("exchange voltage/sdhc support info", s);
		}
		v = card.resp[2]>>8;
		if((v & ACMD41voltagewindow) == 0)
			error("voltage not supported");
		if(v & ACMD41ready) {
			card.sdhc = (v & ACMD41sdhcsupported) != 0;
			break;
		}

		if(i++ >= 100)
			error("sd card failed to power up");
		tsleep(&up->sleep, return0, nil, 10);
	}
	dprint("acmd41 done, mmc %d, sd2 %d, sdhc %d\n", card.mmc, card.sd2, card.sdhc);
	if(card.mmc)
		error("mmc cards not yet supported"); // xxx p14 says this involves sending cmd1

	s = sdcmd(&card, 2, 0, R2, 0);
	if(s < 0)
		errorsd("card identification", s);
	if(parsecid(&card.cid, card.resp) < 0)
		error("bad cid register");

	i = 0;
	for(;;) {
		s = sdcmd(&card, 3, 0, R6, 0);
		if(s < 0)
			errorsd("getting relative address", s);
		card.rca = (card.resp[2]>>24) & MASK(16);
		v = (card.resp[2]>>8) & MASK(16);
		dprint("have card rca %ux, status %lux\n", card.rca, v);
		USED(v);
		if(card.rca != 0)
			break;
		if(i++ == 10)
			error("card insists on invalid rca 0");
	}

	s = sdcmd(&card, 9, card.rca<<16, R2, 0);
	if(s < 0)
		errorsd("get csd", s);
	if(parsecsd(&card.csd, card.resp) < 0)
		error("bad csd register");

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

	s = sdcmd(&card, 7, card.rca<<16, R1b, 0);
	if(s < 0)
		errorsd("selecting card", s);

if(0){
	uchar *p = malloc(64);
	Scr scr;

	s = sdcmd(&card, 16, 64, R1, 0);
	if(s < 0)
		errorsd("setting 64 byte blocksize", s);

	s = sdcmddma(&card, 51, 1<<0, p, 64, 1, R1, Fapp|Fdmad2h);
	if(s < 0)
		errorsd("read scr", s);

	if(parsescr(&scr, card.resp) < 0)
		error("bad scr register");

	print("scr: vers %ud, spec %ud, dataerased %ud, sec %ud, buswidth %ud\n",
		(uint)scr.vers, (uint)scr.spec, (uint)scr.dataerased, (uint)scr.sec, (uint)scr.buswidth);
}

	/* xxx have to check if this is supported by card.  in scr register */
	s = sdcmd(&card, 6, 1<<1, R1, Fapp);
	if(s < 0)
		errorsd("setting buswidth to 4-bit", s);

	s = sdcmd(&card, 16, 512, R1, 0);
	if(s < 0)
		errorsd("setting 512 byte blocksize", s);

	sdiotab[Qdata].length = card.size;
	card.valid = 1;
	print("%s", cardstr(&card, up->genbuf, sizeof (up->genbuf)));
}


static long
sdio(uchar *a, long n, vlong offset, int iswrite)
{
	ulong cmd, arg, fl, h, nn;
	int s;

	if(card.valid == 0)
		error(Enocard);

	/* xxx we should cover this case with a buffer, and then use the same code to allow non-sector-aligned reads? */
	if((ulong)a % 4 != 0)
		error("bad buffer alignment...");

	if(offset & (512-1))
		error("not sector aligned");
	if(n & (512-1))
		error("not multiple of sector size");

	cmd = CMDReadmulti;
	fl = Fdmad2h;
	if(iswrite) {
		cmd = CMDWritemulti;
		fl = Fdmah2d;
	}

	h = 0;
	for(;;) {
		if(card.sdhc)
			arg = (offset+h)/512;
		else
			arg = offset+h;
		nn = n;
		if(nn > 512*1024)
			nn = 512*1024;
		s = sdcmddma(&card, cmd, arg, a+h, 512, nn/512, R1, fl|Fmulti);
		if(s < 0)
			errorsd("io", s);
		h += nn;
		if(h >= n)
			break;

		/* give others a chance */
		if(waserror()) {
			qlock(&sdl);
			nexterror();
		}
		qunlock(&sdl);
		poperror();
		qlock(&sdl);
	}
	return n;
}

static void
sdiointr(Ureg*, void*)
{
	SdioReg *r = SDIOREG;

	/* disable interrupt, st & est are unchanged, caller reads & clears them before next cmd. */
	r->stirq = 0;
	r->estirq = 0;
	intrclear(Irqlo, IRQ0sdio);
	wakeup(&cmdr);
}

static void
sdioreset(void)
{
	SdioReg *r = SDIOREG;

	/* disable all interrupts.  dma interrupt will be enabled as required.  all bits lead to IRQ0sdio. */
	intrdisable(Irqlo, IRQ0sdio, sdiointr, nil, "sdio");
	r->stirq = 0;
	r->estirq = 0;
}

static void
sdioinit(void)
{
	SdioReg *r = SDIOREG;

	intrdisable(Irqlo, IRQ0sdio, sdiointr, nil, "sdio");

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
	intrenable(Irqlo, IRQ0sdio, sdiointr, nil, "sdio");
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

	qlock(&sdl);
	if(waserror()) {
		qunlock(&sdl);
		nexterror();
	}

	switch((ulong)c->qid.path){
	case Qdir:
		n = devdirread(c, a, n, sdiotab, nelem(sdiotab), devgen);
		break;
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

	qunlock(&sdl);
	poperror();

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

	qlock(&sdl);
	if(waserror()) {
		qunlock(&sdl);
		nexterror();
	}

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

	qunlock(&sdl);
	poperror();

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
