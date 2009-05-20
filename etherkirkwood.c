#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "etherif.h"

typedef struct Rx Rx;
struct Rx {
	ulong	cs;
	ulong	countsize;
	uchar	*buf;
	void	*next;
};

typedef struct Tx Tx;
struct Tx {
	ulong	cs;
	ulong	countchk;
	uchar	*buf;
	void	*next;
};

static Rx *rxring;
static int rxnext;

static Tx *txring;
static int txnext;

enum {
	Rxbufsize	= 1024,
	Nrx	= 512,

	Txbufsize	= 1024,
	Ntx	= 512,

	Descralign	= 16,
	Bufalign	= 8,

	Bufsizeshift	= 3,

	ENQshift	= 0,
	DISQshift	= 8,

	PSC0portenable	= 1<<0,

	RCSmacerr	= 1<<0,
	RCSmacmask	= 3<<1,
	RCSmacce	= 0<<1,
	RCSmacor	= 1<<1,
	RCSmacmf	= 2<<1,
	RCSl4chkshift	= 3,
	RCSl4chkmask	= (1<<16)-1,
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
	iprint("%s %02hhux%02hhux%02hhux%02hhux%02hhux%02hhux %02hhux%02hhux%02hhux%02hhux%02hhux%02hhux %02hhux%02hhux%02hhux%02hhux%02hhux%02hhux %02hhux%02hhux%02hhux%02hhux%02hhux%02hhux\n",
		s,
		p[0], p[1], p[2], p[3], p[4], p[5],
		p[6], p[7], p[8], p[9], p[10], p[11],
		p[12], p[13], p[14], p[15], p[16], p[17],
		p[18], p[19], p[20], p[21], p[22], p[23]);
}

static void
kwrecv(Ether *ether)
{
	Rx *r, *p;
	Block *b;
	ulong total, n;
	int i, last;

	for(;;) {
		r = &rxring[rxnext];
		if(r->cs&RCSdmaown)
			break;

		/*
		 * walk through list, calculating full packet size.
		 * we allocate that with iallocb, then fill it.
		 * while filling, we give the descriptors back to dma.
		 * when the packet is in the block, we give it to the ethernet layer.
		 *
		 * xxx this should be done more efficiently, like the driver for the intel gigabit card.
		 */

		if((r->cs&RCSfirst) == 0)
			panic("rx, first in chain has no First flag!?");
		if(r->cs&RCSmacerr) {
			iprint("rx, ether mac error...\n");
			r->cs |= RCSdmaown;
			r->cs |= RCSenableintr;
			rxnext = NEXT(rxnext, Nrx);
			continue;
		}

		total = 0;
		p = r;
		i = rxnext;
		for(;;) {
			if(p->cs&RCSdmaown)
				panic("rx, dma-owned in chain of blocks with leading cpu-owned");
			n = p->countsize>>16;
			total += n;
			last = p->cs&RCSlast;

			p = &rxring[i=NEXT(i, Nrx)];
			if(p == r)
				panic("rx, circular packet?!");

			if(last)
				break;
		}

		b = iallocb(total);
		p = r;
		i = rxnext;
		for(;;) {
			if(b != nil) {
				n = p->countsize>>16;
				memmove(b->wp, p->buf, n);
				b->wp += n;
			}
			p->cs |= RCSdmaown;
			p->cs |= RCSenableintr;
			last = p->cs&RCSlast;

			p = &rxring[i=NEXT(i, Nrx)];

			if(last)
				break;
		}
		if(b != nil)
			b->rp += 2; // xxx ethernet puts two bytes in front?
		rxnext = i;
		
		if(b != nil) {
			if(0)iprint("rx %ld b\n", BLEN(b));
			if(0)dumpheader("rx", b->rp);
			etheriq(ether, b, 1);
		}
	}
}

static void
kwtransmit(Ether *e)
{
	Tx *t0, *t1, *l;
	Block *b;
	int n;
	GbeReg *gbe0 = GBE0REG;

	/*
	 * for now we don't do jumbo frames.
	 * so every packet will fit in 2 descriptors (of 1024 bytes each).
	 * this means we can keep fetching (qget) new packets as long as there are two free transmit descriptors.
	 * we just copy the data into the transmit descriptor buffers for now.
	 * then let the ethernet continue.
	 */

	for(;;) {
		/* need two free descriptors */
		if(txring[NEXT(txnext, Ntx)].cs&TCSdmaown) {
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

		l = t0 = &txring[txnext];
		txnext = NEXT(txnext, Ntx);

		n = BLEN(b);
		if(n > Txbufsize)
			n = Txbufsize;
		memmove(t0->buf, b->rp, n);
		t0->countchk = n<<16;
		if(0)dumpheader("rx", t0->buf);
		b->rp += n;
		n = BLEN(b);
		if(n > 0) {
			iprint("tx more\n");
			l = t1 = &txring[txnext];
			txnext = NEXT(txnext, Ntx);

			n = BLEN(b);
			memmove(t1->buf, b->rp, n);
			t1->countchk = n<<16;
			b->rp += n;

			t1->cs = TCSdmaown;
		}
		t0->cs = TCSfirst;
		l->cs |= TCSlast|TCSdmaown|TCSenableintr;
		if(l != t0)
			t0->cs |= TCSdmaown;
		gbe0->tqc = 1<<(ENQshift+0);

		freeb(b);
	}
}

static void
kwintr(Ureg*, void *arg)
{
	GbeReg *gbe0 = GBE0REG;
	Ether *e = arg;

	// xxx link change
	kwrecv(e);
	kwtransmit(e);

	gbe0->irq = 0;
	gbe0->irqe = 0;
	intrclear(Irqlo, IRQ0gbe0sum);
}

static int
kirkwoodreset(Ether *e)
{
	int i;
	GbeReg *gbe0 = GBE0REG;
	Rx *r;
	Tx *t;

	rxring = xspanalloc(Nrx*sizeof (Rx), Descralign, 0);
	if(rxring == nil)
		panic("no mem for rxring");
	for(i = 0; i < Nrx; i++) {
		r = &rxring[i];
		r->cs = RCSdmaown|RCSenableintr;
		r->countsize = Rxbufsize<<Bufsizeshift;
		r->buf = xspanalloc(Rxbufsize, Bufalign, 0);
		if(r->buf == nil)
			panic("no mem for rxring buf");
		r->next = &rxring[NEXT(i, Nrx)];
	}

	txring = xspanalloc(Ntx*sizeof (Tx), Descralign, 0);
	if(txring == nil)
		panic("no mem for txring");
	for(i = 0; i < Ntx; i++) {
		t = &txring[i];
		t->cs = 0;
		t->countchk = 0;
		t->buf = xspanalloc(Txbufsize, Bufalign, 0);
		if(t->buf == nil)
			panic("no mem for txring buf");
		t->next = &txring[NEXT(i, Ntx)];
	}

	e->attach = nil;
	e->transmit = kwtransmit;
	e->interrupt = kwintr;
	e->ifstat = nil;
	e->shutdown = nil;
	e->ctl = nil;
	e->arg = e;
	e->promiscuous = nil;
	e->multicast = nil;

	gbe0->sdc |= 1<<0; // rx intr only on frame boundary, not descriptor boundary
	gbe0->irqmask = ~0;
	gbe0->irqemask = ~0;
	gbe0->irq = 0;
	gbe0->irqe = 0;
	gbe0->euc &= ~(1<<1); // get out of "polling mode"


	txnext = 0;
	gbe0->tcqdp[0] = (ulong)&txring[txnext];

	rxnext = 0;
	gbe0->crdp[0].r = (ulong)&rxring[rxnext];
	gbe0->rqc = 1<<(ENQshift+0);
	gbe0->psc0 |= PSC0portenable;


	debugkey('e', "etherintr", kwintr, 0);

	return 0;
}

void
etherkirkwoodlink(void)
{
	addethercard("kirkwood", kirkwoodreset);
}
