#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"io.h"
#include	"sdcard.h"

#define DEBUG 0
#define dprint	if(DEBUG)print

char Enocard[] = "no card";

enum {
	SDOk		= 0,
	SDTimeout	= -1,
	SDCardbusy	= -2,
	SDError		= -3,
};

enum {
	/* transfer mode */
	TMdatawrite	= 1<<1,		/* write data after response, for write commands */
	TMautocmd12	= 1<<2,		/* hardware sends cmd12 (note: doc contradicts itself)  */
	TMxfertohost	= 1<<4,		/* data flows from sd to host */
	TMswwrite	= 1<<6,		/* software-controlled, not dma */

	/* cmd */
#define CMDresp(x)	((x)<<0)
	CMDrespnone	= 0<<0,
	CMDresp136	= 1<<0,
	CMDresp48	= 2<<0,
	CMDresp48busy	= 3<<0,

	CMDdatacrccheck	= 1<<2,
	CMDcmdcrccheck	= 1<<3,
	CMDcmdindexcheck= 1<<4,
	CMDdatapresent	= 1<<5,
	CMDunexpresp	= 1<<7,
#define CMDcmd(x)	((x)<<8)

	/* hoststate */
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

	/* st, status */
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

	/* est, error status */
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
	/* sd commands */
	CMDRead		= 17,
	CMDReadmulti	= 18,
	CMDWrite	= 24,
	CMDWritemulti	= 25,
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
	Qdir,
	Qctl,
	Qinfo,
	Qdata,
	Qstatus,
};

static
Dirtab sdiotab[] = {
	".",		{Qdir, 0, QTDIR},	0,	0555,
	"sdioctl",	{Qctl},			0,	0666,
	"sdioinfo",	{Qinfo},		0,	0444,
	"sdio",		{Qdata},		0,	0666,
	"sdiostatus",	{Qstatus},		0,	0444,
};

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

char *acmd12ststrs[] = {
"acmd12notexe",
"acmd12timeout",
"acmd12crcerr",
"acmd12endbiterr",
"acmd12indexerr",
"acmd12resptbi",
"acmd12respstartbiterr",
};

static char *
acmd12ststr(char *p, char *e, ulong v)
{
	int i;
	for(i = 0; i < nelem(acmd12ststrs); i++)
		if(v & (1<<i))
			p = seprint(p, e, " %q", acmd12ststrs[i]);
	return p;
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
	SdioReg *r = SDIOREG;
	ulong need = NScmdcomplete|NSdmaintr;

	if((r->st & need) == need) {
		dprint("dmadone\n");
		return 1;
	}
	dprint("not dmadone\n");
	r->stirq = NSdmaintr;
	return 0;
}

static int
sdcmd0(Card *c, int isapp, ulong cmd, ulong arg)
{
	SdioReg *r = SDIOREG;
	int i, s;
	ulong resptype, cmdopts, mode, v, need;
	uvlong w;

	i = 0;
	for(;;) {
		if((r->hoststate & (HScmdinhibit|HScardbusy)) == 0)
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
	r->st = ~0;
	r->est = ~0;
	r->acmd12st = ~0;
	printstatus("before cmd: ", r->st, r->est, r->acmd12st);

	microdelay(1000);
	/* prepare args & execute command */
	r->arglo = (arg>>0) & MASK(16);
	r->arghi = (arg>>16) & MASK(16);
	cmdopts = 0;
	mode = 0;
	switch(cmd) {
	case CMDRead:
	case CMDReadmulti:
		mode |= TMxfertohost;
		/* fallthrough */
	case CMDWrite:
	case CMDWritemulti:
		cmdopts = CMDdatapresent;
	}

	switch(cmd) {
	case CMDWrite:
	case CMDWritemulti:
		mode |= TMdatawrite;
	}

	switch(cmd) {
	case CMDReadmulti:
	case CMDWritemulti:
		mode |= TMautocmd12;
		r->acmd12arglo = 0;
		r->acmd12arghi = 0;
		r->acmd12idx = AIcheckbusy|AIcheckindex|AICMDINDEX(12);
	}
	r->mode = mode;
	resptype = getresptype(isapp, cmd);
	r->cmd = CMDresp(resptype)|cmdopts|CMDcmd(cmd);

	switch(cmd) {
	case CMDRead:
	case CMDReadmulti:
	case CMDWrite:
	case CMDWritemulti:
		/* wait for dma interrupt that signals completion */
		tsleep(&dmar, dmafinished, nil, 5000);

		need = NScmdcomplete|NSdmaintr;
		if((r->st & need) != need || (r->st & (NSerror|NSunexpresp)) != 0) {
			printstatus("dma err: ", r->st, r->est, r->acmd12st);
			return SDError;
		}
		break;

	default:
		/* poll for completion/error */
		need = NScmdcomplete;
		i = 0;
		for(;;) {
			v = r->st;
			if(v & (NSerror|NSunexpresp)) {
				printstatus("error: ", v, r->est, r->acmd12st);
				if(r->est & EScmdtimeout)
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
	switch(resptype) {
	case CMDrespnone:
		break;
	case CMDresp136:
		w = 0;
		w |= (uvlong)r->resp[7] & MASK(14);
		w |= (uvlong)r->resp[6]<<(0*16+14);
		w |= (uvlong)r->resp[5]<<(1*16+14);
		w |= (uvlong)r->resp[4]<<(2*16+14);
		w |= (uvlong)r->resp[3]<<(3*16+14);
		putle(c->resp+1, w, 8);

		w = 0;
		w |= (uvlong)r->resp[3]>>2;
		w |= (uvlong)r->resp[2]<<(0*16+14);
		w |= (uvlong)r->resp[1]<<(1*16+14);
		w |= (uvlong)r->resp[0]<<(2*16+14);
		putle(c->resp+1+8, w, 8);
		break;
	case CMDresp48:
	case CMDresp48busy:
		w = 0;
		w |= (uvlong)r->resp[2] & MASK(6);
		w |= (uvlong)r->resp[1]<<(0*16+6);
		w |= (uvlong)r->resp[0]<<(1*16+6);
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

static void
sdinit(void)
{
	int i, s;
	ulong v;
	SdioReg *r = SDIOREG;

	sdclock(400*1000);
	r->hostctl &= ~HChighspeed;

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

	if(sdcmd(&card, 7, card.rca<<16) < 0)
		errorsd("selecting card");

	/* xxx have to check if this is supported by card */
	if(sdacmd(&card, 6, (1<<1)) < 0)
		errorsd("setting buswidth to 4-bit");

	if(sdcmd(&card, 16, card.bs) < 0)
		errorsd("setting block length");

	sdiotab[Qdata].length = card.size;
	card.valid = 1;
	print("%s", cardstr(&card, up->genbuf, sizeof (up->genbuf)));
}


static long
sdio(uchar *a, long n, vlong offset, int iswrite)
{
	SdioReg *r = SDIOREG;
	ulong cmd, arg;

	if(card.valid == 0)
		error(Enocard);

	/* xxx we should cover this cases with a buffer, and then use the same code to allow non-sector-aligned reads? */
	if((ulong)a % 4 != 0)
		error("bad buffer alignment...");

	if(offset % card.bs != 0)
		error("not sector aligned");
	if(n % card.bs != 0)
		error("not multiple of sector size");

	dcwbinv(a, n);
	r->dmaaddrlo = (ulong)a & MASK(16);
	r->dmaaddrhi = ((ulong)a>>16) & MASK(16);
	r->blksize = card.bs;
	r->blkcount = n/card.bs;
	if(card.sdhc)
		arg = offset/card.bs;
	else
		arg = offset;
	//tsleep(&up->sleep, return0, nil, 250);
	dprint("sdio, a %#lux, dmaddrlo %#lux dmaadrhi %#lux blksize %lud blkcount %lud cmdarg %#lux\n",
		a, (ulong)a & MASK(16), ((ulong)a>>16) & MASK(16), card.bs, n/card.bs, arg);
	//tsleep(&up->sleep, return0, nil, 250);
	cmd = iswrite ? CMDWritemulti : CMDReadmulti;
	if(sdcmd(&card, cmd, arg) < 0)
		errorsd(iswrite ? "writing" : "reading");

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
	r->stirq &= ~NSdmaintr;
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
	r->hostctl = HCpushpull|HCcardtypememonly|HCbigendian|HCdatawidth4|HCtimeout(15)|HCtimeoutenable;

	/* clear status */
	r->st = ~0;
	r->est = ~0;

	/* enable most status reporting */
	r->stena = ~(NStxready|NSfifo8wavail);
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
		dprint("returning %ld bytes\n", n);
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
