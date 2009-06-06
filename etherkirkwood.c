#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/netif.h"

#include	"etherif.h"

/*
 * todo:
 * - error handling, mostly in descriptors
 * - properly make sure preconditions hold (e.g. being idle) when performing some of the operations (enabling port or queue).
 * - jumbo frames
 * - only enable tx interrupt when descriptors are full but we have something to send?
 * - transmit without copying?
 *
 * features that could be implemented:
 * - ip4,tcp,udp checksum offloading
 * - unicast/multicast filtering
 * - multiple receive queues (e.g. tcp,udp,ethernet priority,other), possibly with priority
 * - receive without copying?
 */

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

struct Ctlr
{
	Lock;
	GbeReg	*reg;

	Rx	*rx;
	int	rxnext;

	Tx	*tx;
	int	txnext;

	/* stats */
	ulong	intrs;
	ulong	newintrs;
	ulong	txunderrun;
	ulong	rxdiscard;
	ulong	rxoverrun;

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


#define MASK(v)	((1<<(v))-1)
enum {
	Rxbufsize	= 1024,
	Nrx		= 512,

	Txbufsize	= 1024,
	Ntx		= 512,

	Descralign	= 16,
	Bufalign	= 8,

	Bufsizeshift	= 3,

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
	/* rx interrupt ipg (inter packet gap), 14 bits.  high bit mirrored at bit 25. */
#define SDCipgintrx(v)	((((v)>>13) & 1)<<25) | (((v) & MASK(14))<<7)

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
	PSC1rgmii	= 1<<2,
	PSC1portreset	= 1<<4,
	PSC1clockbypass	= 1<<5,
	PSC1ibautoneg	= 1<<6,
	PSC1ibautonegbypass	= 1<<7,
	PSC1ibautonegrestart	= 1<<8,
	PSC1gbpsonly	= 1<<11,
	PSC1encolonbp	= 1<<15,	/* "collision during back-pressure mib counting" */
	PSC1coldomainlimitmask	= MASK(6)<<16,
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
#define TFUTipginttx(v)	(((v) & MASK(14))<<4);

	/* minimal frame size, mfs */
	MFS40bytes	= 10<<2,
	MFS44bytes	= 11<<2,
	MFS48bytes	= 12<<2,
	MFS52bytes	= 13<<2,
	MFS56bytes	= 14<<2,
	MFS60bytes	= 15<<2,
	MFS64bytes	= 16<<2,

	/* receive descriptor */
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

	/* transmit descriptor */
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

static void
dumpheader(char *s, uchar *p)
{
	iprint("%s %02ux%02ux%02ux%02ux%02ux%02ux %02ux%02ux%02ux%02ux%02ux%02ux %02ux%02ux%02ux%02ux%02ux%02ux %02ux%02ux%02ux%02ux%02ux%02ux\n",
		s,
		p[0], p[1], p[2], p[3], p[4], p[5],
		p[6], p[7], p[8], p[9], p[10], p[11],
		p[12], p[13], p[14], p[15], p[16], p[17],
		p[18], p[19], p[20], p[21], p[22], p[23]);
}

static void
receive(Ether *e)
{
	Rx *r, *p;
	Block *b;
	ulong total, n;
	int i, last;
	Ctlr *ctlr = e->ctlr;

	for(;;) {
		r = &ctlr->rx[ctlr->rxnext];
		if(r->cs & RCSdmaown)
			break;

		/*
		 * walk through list, calculating full packet size.
		 * we allocate that with iallocb, then fill it.
		 * while filling, we give the descriptors back to dma.
		 * when the packet is in the block, we give it to the ethernet layer.
		 *
		 * xxx this should be done more efficiently, like the driver for the intel gigabit card.
		 */

		if((r->cs & RCSfirst) == 0)
			panic("rx, first in chain has no First flag!?");
		if(r->cs & RCSmacerr) {
			iprint("rx, ether mac error...\n");
			r->cs |= RCSdmaown;
			r->cs |= RCSenableintr;
			ctlr->rxnext = NEXT(ctlr->rxnext, Nrx);
			continue;
		}

		total = 0;
		p = r;
		i = ctlr->rxnext;
		for(;;) {
			if(p->cs & RCSdmaown)
				panic("rx, dma-owned in chain of blocks with leading cpu-owned");
			n = p->countsize>>16;
			total += n;
			last = p->cs & RCSlast;

			p = &ctlr->rx[i=NEXT(i, Nrx)];
			if(p == r)
				panic("rx, circular packet?!");

			if(last)
				break;
		}

		b = iallocb(total);
		p = r;
		i = ctlr->rxnext;
		for(;;) {
			if(b != nil) {
				n = p->countsize>>16;
				memmove(b->wp, (uchar*)p->buf, n);
				b->wp += n;
			}
			p->cs |= RCSdmaown;
			p->cs |= RCSenableintr;
			last = p->cs & RCSlast;

			p = &ctlr->rx[i=NEXT(i, Nrx)];

			if(last)
				break;
		}
		if(b != nil)
			b->rp += 2; // xxx ethernet puts two bytes in front?
		ctlr->rxnext = i;
		
		if(b != nil) {
			if(0)iprint("rx %ld b\n", BLEN(b));
			if(0)dumpheader("rx", b->rp);
			etheriq(e, b, 1);
		}
	}
}

static void
txstart(Ether *e)
{
	Tx *t0, *t1, *l;
	Block *b;
	int n;
	Ctlr *ctlr = e->ctlr;
	GbeReg *reg = ctlr->reg;

	/*
	 * for now we don't do jumbo frames.
	 * so every packet will fit in 2 descriptors (of 1024 bytes each).
	 * this means we can keep fetching (qget) new packets as long as there are two free transmit descriptors.
	 * we just copy the data into the transmit descriptor buffers for now.
	 * then let the ethernet continue.
	 */

	for(;;) {
		/* need two free descriptors */
		if(ctlr->tx[NEXT(ctlr->txnext, Ntx)].cs & TCSdmaown) {
			iprint("tx, tds full\n");
			break;
		}

		b = qget(e->oq);
		if(b == nil)
			break;
		if(BLEN(b) > 2*Txbufsize)
			panic("packet longer than 2*Txbufsize!?");
		if(0)iprint("tx %ld b\n", BLEN(b));
		if(BLEN(b) < 60) {
			iprint("tx short\n");
			continue;
		}

		l = t0 = &ctlr->tx[ctlr->txnext];
		ctlr->txnext = NEXT(ctlr->txnext, Ntx);

		n = BLEN(b);
		if(n > Txbufsize)
			n = Txbufsize;
		memmove((uchar*)t0->buf, b->rp, n);
		t0->countchk = n<<16;
		if(0)dumpheader("rx", (uchar*)t0->buf);
		b->rp += n;
		n = BLEN(b);
		if(n > 0) {
			if(0)iprint("tx more\n");
			l = t1 = &ctlr->tx[ctlr->txnext];
			ctlr->txnext = NEXT(ctlr->txnext, Ntx);

			n = BLEN(b);
			memmove((uchar*)t1->buf, b->rp, n);
			t1->countchk = n<<16;
			b->rp += n;

			t1->cs = TCSdmaown;
		}
		t0->cs = TCSfirst;
		l->cs |= TCSlast|TCSdmaown|TCSenableintr;
		if(l != t0)
			t0->cs |= TCSdmaown;
		reg->tqc = Txqenable(0);

		freeb(b);
	}
}

static void
transmit(Ether *e)
{
	Ctlr *ctlr = e->ctlr;

	ilock(ctlr);
	txstart(e);
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

	ilock(ctlr);
	ctlr->newintrs++;

	irq = reg->irq;
	irqe = reg->irqe;
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
	if(irqe & IEtxbufferq(0))
		txstart(e);

	if(linkchange && (reg->ps1 & PS1autonegdone)) {
		e->link = (reg->ps0 & PS0linkup) != 0;
		linkchange = 0;
	}

	iunlock(ctlr);

	reg->irq = 0;
	reg->irqe = 0;
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
	// xxx
	USED(arg);
	USED(addr);
	USED(on);
}


long
ifstat(Ether *ether, void *a, long n, ulong off)
{
	Ctlr *ctlr = ether->ctlr;
	GbeReg *reg = ctlr->reg;
	char *buf, *p, *e;

	buf = p = smalloc(READSTR);
	e = p+READSTR;

	ilock(ctlr);

	p = seprint(p, e, "link: %d, ps0 %#lux, ps1 %#lux\n", (reg->ps0 & PS0linkup) != 0, reg->ps0, reg->ps1);

	ctlr->intrs += ctlr->newintrs;
	p = seprint(p, e, "interrupts: %lud\n", ctlr->intrs);
	p = seprint(p, e, "new interrupts: %lud\n", ctlr->newintrs);
	ctlr->newintrs = 0;
	p = seprint(p, e, "tx underrun: %lud\n", ctlr->txunderrun);

	ctlr->rxdiscard += reg->pxdfc;
	ctlr->rxoverrun += reg->pxofc;
	p = seprint(p, e, "rx discarded frames: %lud\n", ctlr->rxdiscard);
	p = seprint(p, e, "rx overrun frames: %lud\n", ctlr->rxoverrun);

	p = seprint(p, e, "duplex: %s\n", (reg->ps0 & PS0fullduplex) ? "full" : "half");
	p = seprint(p, e, "flow control: %s\n", (reg->ps0 & PS0flowcontrol) ? "on" : "off");
	//p = seprint(p, e, "speed: %d mbps\n", );

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
	iunlock(ctlr);

	n = readstr(off, a, n, buf);
	free(buf);
	return n;
}

enum {
	CMtest,
};

static Cmdtab ctlmsg[] = {
	CMtest,	"test",	0,
};

long
ctl(Ether *e, void *p, long n)
{
	Ctlr *ctlr = e->ctlr;
	Cmdbuf *cb;
	Cmdtab *ct;

	cb = parsecmd(p, n);
	if(waserror()) {
		free(cb);
		nexterror();
	}

	ct = lookupcmd(cb, ctlmsg, nelem(ctlmsg));
	switch(ct->index) {
	default:
		error(Ebadctl);
	}
	free(cb);
	poperror();
	return n;
}


static void
ctlrinit(Ctlr *ctlr)
{
	Rx *r, *rxring;
	Tx *t, *txring;
	uchar *buf;
	int i;

	rxring = xspanalloc(Nrx*sizeof (Rx), Descralign, 0);
	if(rxring == nil)
		panic("no mem for rxring");
	for(i = 0; i < Nrx; i++) {
		r = &rxring[i];
		r->cs = RCSdmaown|RCSenableintr;
		r->countsize = Rxbufsize<<Bufsizeshift;
		buf = xspanalloc(Rxbufsize, Bufalign, 0);
		if(buf == nil)
			panic("no mem for rxring buf");
		r->buf = (ulong)buf;
		r->next = (ulong)&rxring[NEXT(i, Nrx)];
	}

	txring = xspanalloc(Ntx*sizeof (Tx), Descralign, 0);
	if(txring == nil)
		panic("no mem for txring");
	for(i = 0; i < Ntx; i++) {
		t = &txring[i];
		t->cs = 0;
		t->countchk = 0;
		buf = xspanalloc(Txbufsize, Bufalign, 0);
		if(buf == nil)
			panic("no mem for txring buf");
		t->buf = (ulong)buf;
		t->next = (ulong)&txring[NEXT(i, Ntx)];
	}

	ctlr->rx = rxring;
	ctlr->rxnext = 0;
	ctlr->tx = txring;
	ctlr->txnext = 0;
}

static int
reset(Ether *e)
{
	Ctlr *ctlr;
	GbeReg *reg;

	ctlr = malloc(sizeof ctlr[0]);
	e->ctlr = ctlr;
	switch(e->ctlrno) {
	case 0:
		reg = GBE0REG;
		ctlr->reg = reg;
		break;
	default:
		panic("bad ether ctlr\n");
	}

	ctlrinit(ctlr);

	e->attach = nil;
	e->transmit = transmit;
	e->interrupt = interrupt;
	e->ifstat = ifstat;
	e->shutdown = nil;
	e->ctl = ctl;

	e->arg = e;
	e->promiscuous = promiscuous;
	e->multicast = multicast;

	reg->portcfg = Rxqdefault(0)|Rxqarp(0);
	reg->portcfgx = 0;

	reg->pxmfs = MFS64bytes;
	/*
	 * ipg's (inter packet gaps) do interrupt coalescing.
	 * the values are in 64 clock cycles, max 0x3fff for 1m cycles.
	 */
	reg->sdc = SDCrifb|SDCrxburst(Burst16)|SDCrxnobyteswap|SDCtxnobyteswap|SDCtxburst(Burst16)|SDCipgintrx(~0);
	reg->pxtfut = TFUTipginttx(~0);
	reg->irqmask = ~0;
	reg->irqemask = ~0;
	reg->irq = 0;
	reg->irqe = 0;
	reg->euirqmask = 0;
	reg->euirq = 0;


	reg->tcqdp[0] = (ulong)&ctlr->tx[ctlr->txnext];

	reg->crdp[0].r = (ulong)&ctlr->rx[ctlr->rxnext];
	reg->rqc = Rxqenable(0);
	reg->psc0 |= PSC0portenable;

	e->link = (reg->ps0 & PS0linkup) != 0;

	debugkey('e', "etherintr", interrupt, 0);

	return 0;
}

void
etherkirkwoodlink(void)
{
	addethercard("kirkwood", reset);
}
