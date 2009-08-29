#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/netif.h"

#include	"etherif.h"
#include	"../port/ethermii.h"

#define	MIIDBG	if(0)iprint

/*
 * Marvell 88E1116 gbe controller
 *
 * todo:
 * - properly make sure preconditions hold (e.g. being idle) when performing some of the operations (enabling port or queue).
 *
 * features that could be implemented:
 * - ip4,tcp,udp checksum offloading
 * - unicast/multicast filtering
 * - jumbo frames
 * - multiple receive queues (e.g. tcp,udp,ethernet priority,other), possibly with priority
 */

static struct {
	Lock;
	Block	*head;
	int	init;
} freeblocks;


typedef struct Ctlr Ctlr;
typedef struct Rx Rx;
typedef struct Tx Tx;

struct Rx
{
	ulong	cs;
	ulong	countsize;
	ulong	buf;
	ulong	next;
};

struct Tx
{
	ulong	cs;
	ulong	countchk;
	ulong	buf;
	ulong	next;
};

enum {
	Nrx		= 512,
	Ntx		= 512,

	Rxblocklen	= 2+1522,	/* ethernet uses first two bytes of buffer as padding */
	Nrxblocks	= Nrx+50,

	Descralign	= 16,
	Bufalign	= 8,
};

struct Ctlr
{
	Lock;
	GbeReg	*reg;

	Lock	initlock;
	int	init;

	Rx	*rx;		/* receive descriptors */
	Block	*rxb[Nrx];	/* blocks belonging to the descriptors */
	int	rxhead;		/* next descr ethernet will write to next */
	int	rxtail;		/* next descr that might need a buffer */

	Tx	*tx;
	Block	*txb[Ntx];
	int	txhead;		/* next descr we can use for new packet */
	int	txtail;		/* next descr to reclaim on tx complete */

	Mii	*mii;
	int	port;

	/* stats */
	ulong	intrs;
	ulong	newintrs;
	ulong	txunderrun;
	ulong	txringfull;
	ulong	rxdiscard;
	ulong	rxoverrun;
	ulong	nofirstlast;

	/* mib stats */
	uvlong	rxoctets;
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
	uvlong	txoctets;
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
	ulong	latecollisions;
};


enum {
#define	Rxqenable(q)	(1<<(q))
#define Rxqdisable(q)	(1<<((q)+8)
#define	Txqenable(q)	(1<<(q))
#define Txqdisable(q)	(1<<((q)+8)


	/* sdma config, sdc */
	Burst1		= 0,
	Burst2,
	Burst4,
	Burst8,
	Burst16,
	SDCrifb		= 1<<0,		/* receive interrupt on frame boundaries */
#define SDCrxburst(v)	((v)<<1)
	SDCrxnobyteswap	= 1<<4,
	SDCtxnobyteswap	= 1<<5,
	SDCswap64byte	= 1<<6,
#define SDCtxburst(v)	((v)<<22)
	/* rx interrupt ipg (inter packet gap) */
#define SDCipgintrx(v)	((((v)>>15) & 1)<<25) | (((v) & MASK(15))<<7)

	/* portcfg */
	PCFGupromiscuous= 1<<0,
#define Rxqdefault(q)	((q)<<1)
#define Rxqarp(q)	((q)<<4)
	PCFGbcrejectnoiparp	= 1<<7,
	PCFGbcrejectip		= 1<<8,
	PCFGbcrejectarp		= 1<<9,
	PCFGamnotxes		= 1<<12,	/* automatic mode, no summary update on tx */
	PCFGtcpq	= 1<<14,
	PCFGudpq	= 1<<15,
#define	Rxqtcp(q)	((q)<<16)
#define	Rxqudp(q)	((q)<<19)
#define	Rxqbpdu(q)	((q)<<22)
	PCFGrxcs	= 1<<25,	/* rx tcp checksum mode with header */

	/* portcfgx */
	PCFGXspanq	= 1<<1,
	PCFGXcrcdisable	= 1<<2,		/* no ethernet crc */

	/* port serial control0, psc0 */
	PSC0portenable	= 1<<0,
	PSC0forcelinkup	= 1<<1,
	PSC0autonegduplexdisable	= 1<<2,
	PSC0autonegflowcontroldisable	= 1<<3,
	PSC0autonegpauseadv		= 1<<4,
	PSC0noforcelinkdown	= 1<<10,
	PSC0autonegspeeddisable	= 1<<13,
	PSC0dteadv	= 1<<14,

	PSC0mrumask	= MASK(3)<<17,
#define PSC0mru(v)	((v)<<17)
	PSC0mru1518	= 0,
	PSC0mru1522,
	PSC0mru1552,
	PSC0mru9022,
	PSC0mru9192,
	PSC0mru9700,

	PSC0fullduplexforce	= 1<<21,
	PSC0flowcontrolforce	= 1<<22,
	PSC0gmiispeedgbpsforce	= 1<<23,
	PSC0miispeedforce100mbps= 1<<24,


	/* port status 0, ps0 */
	PS0linkup	= 1<<1,
	PS0fullduplex	= 1<<2,
	PS0flowcontrol	= 1<<3,
	PS0gmiigbps	= 1<<4,
	PS0mii100mbps	= 1<<5,
	PS0txbusy	= 1<<7,
	PS0txfifoempty	= 1<<10,
	PS0rxfifo1empty	= 1<<11,
	PS0rxfifo2empty	= 1<<12,

	/* port serial control1, psc1 */
	PSC1loopback	= 1<<1,
	PSC1mii		= 0<<2,
	PSC1rgmii	= 1<<3,
	PSC1portreset	= 1<<4,
	PSC1clockbypass	= 1<<5,
	PSC1ibautoneg	= 1<<6,
	PSC1ibautonegbypass	= 1<<7,
	PSC1ibautonegrestart	= 1<<8,
	PSC1gbpsonly	= 1<<11,
	PSC1encolonbp	= 1<<15,	/* "collision during back-pressure mib counting" */
	PSC1coldomainlimitmask	= MASK(6)<<16,
#define PSC1coldomainlimit(v)	(((v) & MASK(6))<<16)
	PSC1miiallowoddpreamble	= 1<<22,

	/* port status 1, ps1 */
	PS1rxpause	= 1<<0,
	PS1txpause	= 1<<1,
	PS1pressure	= 1<<2,
	PS1syncfail10ms	= 1<<3,
	PS1autonegdone	= 1<<4,
	PS1inbandautonegbypassed	= 1<<5,
	PS1serdesplllocked	= 1<<6,
	PS1syncok	= 1<<7,
	PS1nosquelch	= 1<<8,

	/* irq */
	Irx		= 1<<0,
	Iextend		= 1<<1,
#define Irxbufferq(q)	(1<<((q)+2))
	Irxerror	= 1<<10,
#define Irxerrorq(q)	(1<<((q)+11))
#define Itxendq(q)	(1<<((q)+19))
	Isum		= 1<<31,

	/* irq extended, irqe */
#define	IEtxbufferq(q)	(1<<((q)+0))
#define	IEtxerrorq(q)	(1<<((q)+8))
	IEphystatuschange	= 1<<16,
	IEptp		= 1<<17,
	IErxoverrun	= 1<<18,
	IEtxunderrun	= 1<<19,
	IElinkchange	= 1<<20,
	IEintaddrerror	= 1<<23,
	IEprbserror	= 1<<25,
	IEsum		= 1<<31,

	/* tx fifo urgent threshold (tx interrupt coalescing), pxtfut */
#define TFUTipginttx(v)	(((v) & MASK(16))<<4);

	/* minimal frame size, mfs */
	MFS40bytes	= 10<<2,
	MFS44bytes	= 11<<2,
	MFS48bytes	= 12<<2,
	MFS52bytes	= 13<<2,
	MFS56bytes	= 14<<2,
	MFS60bytes	= 15<<2,
	MFS64bytes	= 16<<2,


	/* receive descriptor */
#define Bufsize(v)	((v)<<3)

	/* receive descriptor status */
	RCSmacerr	= 1<<0,
	RCSmacmask	= 3<<1,
	RCSmacce	= 0<<1,
	RCSmacor	= 1<<1,
	RCSmacmf	= 2<<1,
	RCSl4chkshift	= 3,
	RCSl4chkmask	= MASK(16),
	RCSvlan		= 1<<17,
	RCSbpdu		= 1<<18,
	RCSlayer4mask	= 3<<21,
	RCSlayer4tcp4	= 0<<21,
	RCSlayer4udp4	= 1<<21,
	RCSlayer4other	= 2<<21,
	RCSlayer4rsvd	= 3<<21,
	RCSlayer2ev2	= 1<<23,
	RCSl3ip4	= 1<<24,
	RCSip4headok	= 1<<25,
	RCSlast		= 1<<26,
	RCSfirst	= 1<<27,
	RCSunknownaddr	= 1<<28,
	RCSenableintr	= 1<<29,
	RCSl4chkok	= 1<<30,
	RCSdmaown	= 1<<31,

	/* transmit descriptor status */
	TCSmacerr	= 1<<0,
	TCSmacmask	= 3<<1,
	TCSmaclc	= 0<<1,
	TCSmacur	= 1<<1,
	TCSmacrl	= 2<<1,
	TCSllc		= 1<<9,
	TCSl4chkmode	= 1<<10,
	TCSipv4hdlenshift	= 11,
	TCSvlan		= 1<<15,
	TCSl4type	= 1<<16,
	TCSgl4chk	= 1<<17,
	TCSgip4chk	= 1<<18,
	TCSpadding	= 1<<19,
	TCSlast		= 1<<20,
	TCSfirst	= 1<<21,
	TCSenableintr	= 1<<23,
	TCSautomode	= 1<<30,
	TCSdmaown	= 1<<31,
};


/*
 * xxx buffers should really be per controller, not per driver as it is now.
 * or otherwise just allocate more buffers for each controller.
 */
static Block *
rxallocb(void)
{
	Block *b;

	ilock(&freeblocks);
	b = freeblocks.head;
	if(b != nil) {
		freeblocks.head = b->next;
		b->next = nil;
	}
	iunlock(&freeblocks);
	return b;
}

static void
rxfreeb(Block *b)
{
	ilock(&freeblocks);
	b->rp = (uchar*)((uintptr)(b->lim-Rxblocklen) & ~(Bufalign-1));
	b->wp = b->rp;

	b->next = freeblocks.head;
	freeblocks.head = b;
	iunlock(&freeblocks);
}

static void
rxreplenish(Ctlr *ctlr)
{
	Rx *r;
	Block *b;

	while(ctlr->rxb[ctlr->rxtail] == nil) {
		b = rxallocb();
		if(b == nil){
			iprint("no available buffers\n");
			break;
		}

		ctlr->rxb[ctlr->rxtail] = b;
		r = &ctlr->rx[ctlr->rxtail];
		r->countsize = Bufsize(Rxblocklen);
		r->buf = (ulong)b->rp;
		r->cs = RCSdmaown|RCSenableintr;
		ctlr->rxtail = NEXT(ctlr->rxtail, Nrx);
	}
}

static void
receive(Ether *e)
{
	Ctlr *ctlr = e->ctlr;
	Rx *r;
	Block *b;
	ulong n;

	for(;;) {
		r = &ctlr->rx[ctlr->rxhead];
		if(r->cs & RCSdmaown)
			break;

		b = ctlr->rxb[ctlr->rxhead];
		ctlr->rxb[ctlr->rxhead] = nil;
		ctlr->rxhead = NEXT(ctlr->rxhead, Nrx);

		if(r->cs & RCSmacerr) {
			freeb(b);
			continue;
		}
		if((r->cs & (RCSfirst|RCSlast)) != (RCSfirst|RCSlast)) {
			ctlr->nofirstlast++;
			freeb(b);
			continue;
		}

		n = r->countsize>>16;
		b->wp = b->rp+n;
		b->rp += 2;	/* padding bytes, hardware inserts it to align ip4 address in memory */

		if(b != nil)
			etheriq(e, b, 1);
	}
	rxreplenish(ctlr);
}

static void
transmit(Ether *e)
{
	Ctlr *ctlr = e->ctlr;
	GbeReg *reg = ctlr->reg;
	Tx *t;
	Block *b;

	ilock(ctlr);
	/* free transmitted packets */
	while(ctlr->txtail != ctlr->txhead && (ctlr->tx[ctlr->txtail].cs & TCSdmaown) == 0) {
		if(ctlr->txb[ctlr->txtail] == nil)
			panic("no block for sent packet?!");
		freeb(ctlr->txb[ctlr->txtail]);
		ctlr->txb[ctlr->txtail] = nil;
		ctlr->txtail = NEXT(ctlr->txtail, Ntx);
	}

	/* queue new packets */
	while(qcanread(e->oq)) {
		t = &ctlr->tx[ctlr->txhead];
		if(t->cs & TCSdmaown) {
			ctlr->txringfull++;
			break;
		}

		b = qget(e->oq);
		if(BLEN(b) < e->minmtu || BLEN(b) > e->maxmtu) {
			freeb(b);
			continue;
		}
		ctlr->txb[ctlr->txhead] = b;
		t->countchk = BLEN(b)<<16;
		t->buf = (ulong)b->rp;
		t->cs = TCSpadding|TCSfirst|TCSlast|TCSenableintr|TCSdmaown;
		reg->tqc = Txqenable(0);
		
		ctlr->txhead = NEXT(ctlr->txhead, Ntx);
	}
	iunlock(ctlr);
}

static void
interrupt(Ureg*, void *arg)
{
	Ether *e = arg;
	Ctlr *ctlr = e->ctlr;
	GbeReg *reg = ctlr->reg;
	ulong irq, irqe;
	static int linkchange = 0;

	ctlr->newintrs++;

	irq = reg->irq;
	irqe = reg->irqe;
	reg->irq = 0;
	reg->irqe = 0;
	if(irqe & IEsum) {
		/*
		 * IElinkchange appears to only be set when unplugging.
		 * autonegotiation is probably not done yet, so linkup not valid,
		 * that's why we note the link change here, and check for
		 * that and autonegotiation done below.
		 */
		if(irqe & IEphystatuschange) {
			e->link = (reg->ps0 & PS0linkup) != 0;
			linkchange = 1;
		}

		if(irqe & IEtxerrorq(0))
			e->oerrs++;
		if(irqe & IErxoverrun)
			e->overflows++;
		if(irqe & IEtxunderrun)
			ctlr->txunderrun++;
	}

	if(irq & Irxbufferq(0))
		receive(e);
	if(qcanread(e->oq) && (irqe & IEtxbufferq(0)))
		transmit(e);

	if(linkchange && (reg->ps1 & PS1autonegdone)) {
		e->link = (reg->ps0 & PS0linkup) != 0;
		linkchange = 0;
	}

	intrclear(Irqlo, IRQ0gbe0sum);
}


void
promiscuous(void *arg, int on)
{
	Ether *e = arg;
	Ctlr *ctlr = e->ctlr;
	GbeReg *reg = ctlr->reg;

	ilock(ctlr);
	if(on)
		reg->portcfg |= PCFGupromiscuous;
	else
		reg->portcfg &= ~PCFGupromiscuous;
	iunlock(ctlr);
}

void
multicast(void *arg, uchar *addr, int on)
{
	/*
	 * xxx do we need to do anything?
	 * we can explicitly filter (pass/block) on multicast addresses if we want...
	 */
	USED(arg, addr, on);
}


static void
getmibstats(Ctlr *ctlr)
{
	GbeReg *reg = ctlr->reg;

	/*
	 * xxx
	 * rxoctectslo & txoctetslo seem to return the same as the *hi-variant.
	 * the docs claim [rt]xoctets 64 bit.  can we do an atomic 64 bit read?
	 */

	/* mib registers clear on read, store them */
	ctlr->rxoctets += reg->rxoctetslo;
	ctlr->badrxoctets += reg->badrxoctets;
	ctlr->mactxerror += reg->mactxerror;
	ctlr->rxframes += reg->rxframes;
	ctlr->badrxframes += reg->badrxframes;
	ctlr->rxbroadcastframes += reg->rxbroadcastframes;
	ctlr->rxmulticastframes += reg->rxmulticastframes;
	ctlr->rxframe64 += reg->rxframe64;
	ctlr->rxframe65to127 += reg->rxframe65to127;
	ctlr->rxframe128to255 += reg->rxframe128to255;
	ctlr->rxframe256to511 += reg->rxframe256to511;
	ctlr->rxframe512to1023 += reg->rxframe512to1023;
	ctlr->rxframe1024tomax += reg->rxframe1024tomax;
	ctlr->txoctets += reg->txoctetslo;
	ctlr->txframes += reg->txframes;
	ctlr->txcollisionframedrop += reg->txcollisionframedrop;
	ctlr->txmulticastframes += reg->txmulticastframes;
	ctlr->txbroadcastframes += reg->txbroadcastframes;
	ctlr->badmaccontrolframes += reg->badmaccontrolframes;
	ctlr->txflowcontrol += reg->txflowcontrol;
	ctlr->rxflowcontrol += reg->rxflowcontrol;
	ctlr->badrxflowcontrol += reg->badrxflowcontrol;
	ctlr->rxundersized += reg->rxundersized;
	ctlr->rxfragments += reg->rxfragments;
	ctlr->rxoversized += reg->rxoversized;
	ctlr->rxjabber += reg->rxjabber;
	ctlr->rxerrors += reg->rxerrors;
	ctlr->crcerrors += reg->crcerrors;
	ctlr->collisions += reg->collisions;
	ctlr->latecollisions += reg->latecollisions;
}


long
ifstat(Ether *ether, void *a, long n, ulong off)
{
	Ctlr *ctlr = ether->ctlr;
	GbeReg *reg = ctlr->reg;
	char *buf, *p, *e;

	ilock(&ctlr->initlock);
	buf = p = malloc(2*READSTR);
	e = p+2*READSTR;

	getmibstats(ctlr);

	ctlr->intrs += ctlr->newintrs;
	p = seprint(p, e, "interrupts: %lud\n", ctlr->intrs);
	p = seprint(p, e, "new interrupts: %lud\n", ctlr->newintrs);
	ctlr->newintrs = 0;
	p = seprint(p, e, "tx underrun: %lud\n", ctlr->txunderrun);
	p = seprint(p, e, "tx ring full: %lud\n", ctlr->txringfull);

	ctlr->rxdiscard += reg->pxdfc;
	ctlr->rxoverrun += reg->pxofc;
	p = seprint(p, e, "rx discarded frames: %lud\n", ctlr->rxdiscard);
	p = seprint(p, e, "rx overrun frames: %lud\n", ctlr->rxoverrun);
	p = seprint(p, e, "no first+last flag: %lud\n", ctlr->nofirstlast);

	p = seprint(p, e, "duplex: %s\n", (reg->ps0 & PS0fullduplex) ? "full" : "half");
	p = seprint(p, e, "flow control: %s\n", (reg->ps0 & PS0flowcontrol) ? "on" : "off");
	//p = seprint(p, e, "speed: %d mbps\n", );

	p = seprint(p, e, "received octets: %llud\n", ctlr->rxoctets);
	p = seprint(p, e, "bad received octets: %lud\n", ctlr->badrxoctets);
	p = seprint(p, e, "internal mac transmit errors: %lud\n", ctlr->mactxerror);
	p = seprint(p, e, "total received frames: %lud\n", ctlr->rxframes);
	p = seprint(p, e, "received broadcast frames: %lud\n", ctlr->rxbroadcastframes);
	p = seprint(p, e, "received multicast frames: %lud\n", ctlr->rxmulticastframes);
	p = seprint(p, e, "bad received frames: %lud\n", ctlr->badrxframes);
	p = seprint(p, e, "received frames 0-64: %lud\n", ctlr->rxframe64);
	p = seprint(p, e, "received frames 65-127: %lud\n", ctlr->rxframe65to127);
	p = seprint(p, e, "received frames 128-255: %lud\n", ctlr->rxframe128to255);
	p = seprint(p, e, "received frames 256-511: %lud\n", ctlr->rxframe256to511);
	p = seprint(p, e, "received frames 512-1023: %lud\n", ctlr->rxframe512to1023);
	p = seprint(p, e, "received frames 1024-max: %lud\n", ctlr->rxframe1024tomax);
	p = seprint(p, e, "transmitted octects: %llud\n", ctlr->txoctets);
	p = seprint(p, e, "total transmitted frames: %lud\n", ctlr->txframes);
	p = seprint(p, e, "transmitted broadcast frames: %lud\n", ctlr->txbroadcastframes);
	p = seprint(p, e, "transmitted multicast frames: %lud\n", ctlr->txmulticastframes);
	p = seprint(p, e, "transmit frames dropped by collision: %lud\n", ctlr->txcollisionframedrop);
	p = seprint(p, e, "bad mac control frames: %lud\n", ctlr->badmaccontrolframes);
	p = seprint(p, e, "transmitted flow control messages: %lud\n", ctlr->txflowcontrol);
	p = seprint(p, e, "received flow control messages: %lud\n", ctlr->rxflowcontrol);
	p = seprint(p, e, "bad received flow control messages: %lud\n", ctlr->badrxflowcontrol);
	p = seprint(p, e, "received undersized packets: %lud\n", ctlr->rxundersized);
	p = seprint(p, e, "received fragments: %lud\n", ctlr->rxfragments);
	p = seprint(p, e, "received oversized packets: %lud\n", ctlr->rxoversized);
	p = seprint(p, e, "received jabber packets: %lud\n", ctlr->rxjabber);
	p = seprint(p, e, "mac receive errors: %lud\n", ctlr->rxerrors);
	p = seprint(p, e, "crc errors: %lud\n", ctlr->crcerrors);
	p = seprint(p, e, "collisions: %lud\n", ctlr->collisions);
	p = seprint(p, e, "late collisions: %lud\n", ctlr->latecollisions);
	USED(p);

	n = readstr(off, a, n, buf);
	free(buf);
	iunlock(&ctlr->initlock);

	return n;
}

static void
shutdown(Ether *e)
{
	Ctlr *ctlr = e->ctlr;
	GbeReg *reg = ctlr->reg;

	ilock(ctlr);
	reg->psc1 |= PSC1portreset;
	iunlock(ctlr);
}

enum {
	CMjumbo,
};

static Cmdtab ctlmsg[] = {
	CMjumbo,	"jumbo",	2,
};

long
ctl(Ether *e, void *p, long n)
{
	Ctlr *ctlr = e->ctlr;
	GbeReg *reg = ctlr->reg;
	Cmdbuf *cb;
	Cmdtab *ct;

	cb = parsecmd(p, n);
	if(waserror()) {
		free(cb);
		nexterror();
	}

	ct = lookupcmd(cb, ctlmsg, nelem(ctlmsg));
	switch(ct->index) {
	case CMjumbo:
		if(strcmp(cb->f[1], "on") == 0) {
			/* incoming packet queue doesn't expect jumbo frames */
			error("jumbo disabled");
			reg->psc0 = (reg->psc0 & ~PSC0mrumask) | PSC0mru(PSC0mru9022);
			e->maxmtu = 9022;
		} else if(strcmp(cb->f[1] , "off") == 0) {
			reg->psc0 = (reg->psc0 & ~PSC0mrumask) | PSC0mru(PSC0mru1522);
			e->maxmtu = 1522;
		} else
			error(Ebadctl);
	default:
		error(Ebadctl);
	}
	free(cb);
	poperror();
	return n;
}

enum
{
	/* SMI regs */
	PhySmiTimeout	= 10000,
	PhySmiDataOff	= 0,				// Data
	PhySmiDataMsk	= 0xffff<<PhySmiDataOff,
		
	PhySmiAddrOff 	= 16,				// PHY device addr
	PhySmiAddrMsk	= 0x1f << PhySmiAddrOff,

	PhySmiOpcode	= 26,
	PhySmiOpcodeMsk	= 3<<PhySmiOpcode,
	PhySmiOpcodeWr	= 0<<PhySmiOpcode,
	PhySmiOpcodeRd	= 1<<PhySmiOpcode,
	
	PhySmiReadValid	= 1<<27,
	PhySmiBusy	= 1<<28,
	
	SmiRegAddrOff	= 21,				// PHY device register addr
	SmiRegAddrMsk	= 0x1f << SmiRegAddrOff,
	
};

static int
smibusywait(GbeReg *reg, int waitbit)
{
	ulong timeout, smi_reg;
	
	timeout = PhySmiTimeout;
	do {
		smi_reg = reg->smi;
		if (timeout-- == 0) {
			MIIDBG("SMI busy timeout %x\n", waitbit);
			return -1;
		}
	} while (smi_reg & waitbit);
	return 0;
}

static int
miird(Mii *mii, int pa, int ra)
{
	Ctlr *ctlr;
	GbeReg *reg;
	ulong smi_reg;
	ulong timeout;

	ctlr = (Ctlr*)mii->ctlr;
	reg = ctlr->reg;
	
	// check to read params
	if (pa == 0xEE && ra == 0xEE)
		return reg->phy & 0x00ff;

	// check params
	if (pa<<PhySmiAddrOff & ~PhySmiAddrMsk)
		return -1;
	if (ra<<SmiRegAddrOff & ~SmiRegAddrMsk)
		return -1;
	
	smibusywait(reg, PhySmiBusy);

	/* fill the phy address and regiser offset and read opcode */
	smi_reg = (pa<<PhySmiAddrOff) | (ra<<SmiRegAddrOff) | PhySmiOpcodeRd;
	reg->smi = smi_reg;
	
	/*wait till readed value is ready */
	if(smibusywait(reg, PhySmiReadValid) < 0)
		return -1;

	/* Wait for the data to update in the SMI register */
	for (timeout = 0; timeout < PhySmiTimeout; timeout++)
		{}
	
	return reg->smi & PhySmiDataMsk;
}

static int
miiwr(Mii *mii, int pa, int ra, int v)
{
	Ctlr *ctlr;
	GbeReg *reg;
	ulong smi_reg;

	ctlr = (Ctlr*)mii->ctlr;
	reg = ctlr->reg;
	
	// check params
	if (pa<<PhySmiAddrOff & ~PhySmiAddrMsk)
		return -1;
	if (ra<<SmiRegAddrOff & ~SmiRegAddrMsk)
		return -1;
	
	smibusywait(reg, PhySmiBusy);
	
	/* fill the phy address and regiser offset and read opcode */
	smi_reg = v<<PhySmiDataOff;
	smi_reg |= (pa<<PhySmiAddrOff) | (ra<<SmiRegAddrOff);
	smi_reg &= ~ PhySmiOpcodeRd;

	reg->smi = smi_reg;
	
	return 0;
}

static int
kirkwoodmii(Ctlr *ctlr)
{
	MiiPhy *phy;
	int i;

	MIIDBG("mii\n");
	if((ctlr->mii = malloc(sizeof(Mii))) == nil)
		return -1;
	ctlr->mii->ctlr = ctlr;
	ctlr->mii->mir = miird;
	ctlr->mii->miw = miiwr;
	
	if(mii(ctlr->mii, ~0) == 0 || (phy = ctlr->mii->curphy) == nil){
		free(ctlr->mii);
		ctlr->mii = nil;
		iprint("etherkirkwood: init mii failure\n");
		return -1;
	}

	MIIDBG("oui %X phyno %d\n", phy->oui, phy->phyno);
	if(miistatus(ctlr->mii) < 0){

		miireset(ctlr->mii);
		MIIDBG("miireset\n");
		if(miiane(ctlr->mii, ~0, 0, ~0) < 0){
			iprint("miiane failed\n");
			return -1;
		}
		MIIDBG("miistatus\n");
		miistatus(ctlr->mii);
		if(miird(ctlr->mii, phy->phyno, Bmsr) & BmsrLs){
			for(i=0;; i++){
				if(i > 600){
					iprint("kirkwood%d: autonegotiation failed\n", ctlr->port);
					break;
				}
				if(miird(ctlr->mii, phy->phyno, Bmsr) & BmsrAnc)
					break;
				delay(10);
			}
			if(miistatus(ctlr->mii) < 0)
				iprint("miistatus failed\n");
		}else{
			iprint("kirkwood%d: no link\n", ctlr->port);
			phy->speed = 10;	/* simple default */
		}
	}

	iprint("kirkwood%d mii: fd=%d speed=%d tfc=%d rfc=%d\n", ctlr->port, phy->fd, phy->speed, phy->tfc, phy->rfc);

	MIIDBG("mii done\n");

	return 0;
}

static int
miiphyinit(Mii *mii)
{
	ulong reg;
	ulong devadr;

	// select mii phy
	devadr = miird(mii, 0xEE, 0xEE);
	print("devadr %lux\n", devadr);
	if (devadr == -1) {
		print("Error..could not read PHY dev address\n");
		return -1;
	}

	// leds link & activity
	miiwr(mii, devadr, 22, 0x3);
	reg = miird(mii, devadr, 10);
	reg &= ~0xf;
	reg |= 0x1;
	miiwr(mii, devadr, 10, reg);
	miiwr(mii, devadr, 22, 0);

	// enable RGMII delay on Tx and Rx for CPU port
	miiwr(mii, devadr, 22, 2);
	reg = miird(mii, devadr, 21);
	reg |= (1<<5) | (1<<4);
	miiwr(mii, devadr, 21, reg);
	miiwr(mii, devadr, 22, 0);
	return 0;
}

static void
portreset(GbeReg *reg)
{	
	ulong v, i;
	
	v = reg->tqc;
	if (v & 0xff) {
		/* Stop & Wait for all Tx activity to terminate. */
		reg->tqc = v << 8;
		while (reg->tqc & 0xff)
			{}
	}

	v = reg->rqc;
	if (v & 0xff) {
		reg->rqc = v << 8;
		/* Stop & Wait for all Rx activity to terminate. */
		while (reg->rqc & 0xff);
			{}
	}

	/* enable port */
	reg->psc0 &= ~PSC0portenable;
	/* Set port & MMI active */
	reg->psc1 &= ~(PSC1rgmii|PSC1portreset);

	for (i = 0; i < 4000; i++)
		{}
}

static void
ctlrinit(Ether *e)
{
	Ctlr *ctlr = e->ctlr;
	GbeReg *reg = ctlr->reg;
	Ctlr fakectlr;
	Rx *r;
	Tx *t;
	int i;
	Block *b;

	ilock(&freeblocks);
	if(freeblocks.init == 0) {
		for(i = 0; i < Nrxblocks; i++) {
			b = iallocb(Rxblocklen+Bufalign-1);
			if(b == nil) {
				iprint("no memory for rxring\n");
				break;
			}
			b->free = rxfreeb;
			b->next = freeblocks.head;
			freeblocks.head = b;
		}
		freeblocks.init = 1;
	}
	iunlock(&freeblocks);

	ctlr->rx = xspanalloc(Nrx*sizeof (Rx), Descralign, 0);
	if(ctlr->rx == nil)
		panic("no memory for rxring");
	for(i = 0; i < Nrx; i++) {
		r = &ctlr->rx[i];
		r->cs = 0;
		r->next = (ulong)&ctlr->rx[NEXT(i, Nrx)];
		ctlr->rxb[i] = nil;
	}
	ctlr->rxtail = 0;
	ctlr->rxhead = 0;
	rxreplenish(ctlr);

	ctlr->tx = xspanalloc(Ntx*sizeof (Tx), Descralign, 0);
	if(ctlr->tx == nil)
		panic("no memory for txring");
	for(i = 0; i < Ntx; i++) {
		t = &ctlr->tx[i];
		t->cs = 0;
		t->next = (ulong)&ctlr->tx[NEXT(i, Ntx)];
		ctlr->txb[i] = nil;
	}
	ctlr->txtail = 0;
	ctlr->txhead = 0;
	
	/* clear stats by reading them into fake ctlr */
	getmibstats(&fakectlr);

	reg->portcfg = Rxqdefault(0)|Rxqarp(0);
	reg->portcfgx = 0;

	reg->pxmfs = MFS64bytes;

	reg->sdc = SDCrifb|SDCrxburst(Burst16)|SDCrxnobyteswap|SDCtxnobyteswap|SDCtxburst(Burst16);

	/* ipg's (inter packet gaps) for interrupt coalescing, values in units of 64 clock cycles */
	reg->sdc |= SDCipgintrx(CLOCKFREQ/(800*64));
	reg->pxtfut = TFUTipginttx(CLOCKFREQ/(800*64));

	reg->irqmask = ~0;
	reg->irqemask = ~0;
	reg->irq = 0;
	reg->irqe = 0;
	reg->euirqmask = 0;
	reg->euirq = 0;

	reg->tcqdp[0] = (ulong)&ctlr->tx[ctlr->txhead];

	reg->crdp[0].r = (ulong)&ctlr->rx[ctlr->rxhead];
	archetheraddr(e, reg, 0);

	reg->rqc = Rxqenable(0);
	reg->psc1 = PSC1rgmii|PSC1encolonbp|PSC1coldomainlimit(0x23);
	reg->psc0 = PSC0portenable|PSC0autonegflowcontroldisable|PSC0autonegpauseadv|PSC0noforcelinkdown|PSC0mru(PSC0mru1522);

	e->link = (reg->ps0 & PS0linkup) != 0;
	
	/* set ethernet MTU for leaky bucket mechanism to 0 (disabled) */
	reg->pmtu = 0;
}

static void
attach(Ether* e)
{
	Ctlr *ctlr = e->ctlr;

	lock(&ctlr->initlock);
	if(ctlr->init == 0) {
		ctlrinit(e);
		ctlr->init = 1;
	}
	unlock(&ctlr->initlock);
}

static int
reset(Ether *e)
{
	Ctlr *ctlr;

	ctlr = malloc(sizeof ctlr[0]);
	e->ctlr = ctlr;
	switch(e->ctlrno) {
	case 0:
		ctlr->reg = GBE0REG;
		break;
	default:
		panic("bad ether ctlr\n");
	}

	portreset(ctlr->reg);
	
	/* Set phy address of the port, see archether */
	ctlr->port = e->ctlrno;
	ctlr->reg->phy = e->ctlrno;
	
	if(kirkwoodmii(ctlr) < 0){
		free(ctlr);
		return -1;
	}
	//miiphyinit(ctlr->mii);

	/* xxx should probably not do this.  can hostowner set mtu, overriding us? */
	if(e->maxmtu > Rxblocklen-2)
		e->maxmtu = Rxblocklen-2;

	e->attach = attach;
	e->transmit = transmit;
	e->interrupt = interrupt;
	e->ifstat = ifstat;
	e->shutdown = shutdown;
	e->ctl = ctl;

	e->arg = e;
	e->promiscuous = promiscuous;
	e->multicast = multicast;

	return 0;
}

void
etherkirkwoodlink(void)
{
	addethercard("kirkwood", reset);
}
