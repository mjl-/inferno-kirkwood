#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"io.h"

enum {
	/* configuration, cfg */
	ModeMask	= MASK(3),
	ModeXor		= 0,
	ModeCrc32c	= 1,
	ModeDma		= 2,
	ModeMeminit	= 4,
	Burst32		= 2,
	Burst64		= 3,
	Burst128	= 4,
#define	Srcburst(v)	(((v) & MASK(3))<<4)
#define	Dstburst(v)	(((v) & MASK(3))<<8)
	Read64swap	= 1<<12,
	Write64swap	= 1<<13,
	Descr64swap	= 1<<14,
	CfgRegaccprotect= 1<<15,

	/* activation, act */
	XEstart		= 1<<0,
	XEstop		= 1<<1,
	XEpause		= 1<<2,
	XErestart	= 1<<3,
	XEstatusmask	= 3<<4,
	XEstatusinactive= 0<<4,
	XEstatusactive	= 1<<4,
	XEstatuspaused	= 2<<4,

	/* interrupt, irq/irqmask */
	Iendofdescr	= 1<<0,
	Iendofchain	= 1<<1,
	Istopped	= 1<<2,
	Ipaused		= 1<<3,
	Iaddrdecode	= 1<<4,
	Iaccprot	= 1<<5,
	Iwriteprot	= 1<<6,
	Iownerr		= 1<<7,
	Iparityerr	= 1<<8,
	Ibarerr		= 1<<9,

	/* window control, winctl */
#define	Window(w)	(1<<(w))
#define Windowacl(w, v)	((v)<<(16+2*w))
	AccessNone	= 0,
	AccessRO	= 1,
	AccessRW	= 3,

	/* window base address, bar */
#define Wintarget(v)	(((v) & MASK(4))<<0)
#define Winattr(v)	(((v) & MASK(8))<<8)
#define Winbase(v)	(((v/(64*1024)) & MASK(16))<<16)

	/* window size, size */
#define Winsize(v)	(((v/(64*1024)-1) & MASK(16))<<16)

	/* descriptor, status */
	Ssuccess	= 1<<30,
	Sdmaowned	= 1<<31,

	/* descriptor, cmd */
	Ccrclast	= 1<<30,
	Cendofdescrirq	= 1<<31,

	Descralign	= 32,
	XDescralign	= 64,
};

typedef struct Descr Descr;
typedef struct XDescr XDescr;
struct Descr
{
	ulong	status;
	ulong	crc32;
	ulong	cmd;
	ulong	nextdescr;
	ulong	bytecount;
	ulong	dest;
	ulong	src;
	ulong	_src2;
};

struct XDescr
{
	ulong	status;
	ulong	crc32;
	ulong	cmd;
	ulong	nextdescr;
	ulong	bytecount;
	ulong	dest;
	ulong	src[8];
	ulong	pad[2];
};


/*
 * Port.inuse
 */
enum {
	Punused,
	Pother,
	Pmeminit,
};
typedef struct Port Port;
struct Port
{
	XorReg	*pr;
	XoreReg	*er;
	int	done;
	int	inuse;
	Rendez;
};

static struct {
	Port ports[4];
	QLock;
	Rendez;
} xor = {
	{
		{AddrXore0p0,	AddrXore0},
		{AddrXore0p1,	AddrXore0},
		{AddrXore1p0,	AddrXore1},
		{AddrXore1p1,	AddrXore1},
	}
};


static ulong
roundup(ulong v, int size)
{
	return (v+size-1)&~(size-1);
}

static Port *
meminitport(Port p[])
{
	if(!p[0].inuse && p[1].inuse != Pmeminit)
		return p+0;
	if(!p[1].inuse && p[0].inuse != Pmeminit)
		return p+1;
	return nil;
}

static Port *
portget(int ismeminit)
{
	Port *p;
	int i;

	p = nil;
	for(;;) {
		qlock(&xor);
		if(ismeminit) {
			p = meminitport(xor.ports);
			if(p == nil)
				p = meminitport(xor.ports+2);
		} else {
			for(i = 0; i < nelem(xor.ports); i++) {
				if(!xor.ports[i].inuse) {
					p = &xor.ports[i];
					break;
				}
			}
		}
		if(p != nil)
			break;
		qunlock(&xor);
		sleep(&xor, return0, nil);
	}
	p->inuse = ismeminit ? Pmeminit : Pother;
	p->done = 0;
	qunlock(&xor);
	return p;
}

static void
portput(Port *p)
{
	qlock(&xor);
	p->inuse = 0;
	wakeup(&xor);
	qunlock(&xor);
}

static int
isdone(void *p)
{
	return ((Port*)p)->done;
}

/*
 * we only program one descriptor at a time.
 * so we only use end-of-chain interrupts for completion.
 * if any other occurs, it's an error.
 */
static void
xorintr(Ureg *, void *port)
{
	Port *p;
	ulong irq;

	p = port;
	//iprint("xorintr, port %#p, irq %#lux\n", p, p->er->irq);
	irq = p->er->irq;
	if(irq & ~(Iendofchain|(Iendofchain<<16)))
		panic("unexpected xor interrupt, port %#p, irq %#lux, %#lux, erroraddr %#lux", port, irq, p->er->errorcause, p->er->erroraddr);
	if(irq & Iendofchain){
		p->done = 1;
		wakeup(p);
	}
	if(irq & (Iendofchain<<16)) {
		p = p+1;
		p->done = 1;
		wakeup(p);
	}
	p->er->irq = 0;
}

static void
xorintrerror(Ureg*, void *port)
{
	Port *p = port;
	panic("xor engine error, cause %#lux, addr %#lux\n", p->er->errorcause, p->er->erroraddr);
}

static void
initport(XorReg *r)
{
	r->act = XEstop;
	r->winctl = Window(0)|Windowacl(0, AccessRW);
	r->winctl |= Window(1)|Windowacl(1, AccessRW);
	r->aoctl = 0;
}

static void
initengine(XoreReg *r)
{
	r->irq = 0;
	r->irqmask = ~0;
	r->bar[0] = Wintarget(0)|Winattr(0xE)|Winbase(0);
	r->bar[1] = Wintarget(0)|Winattr(0xD)|Winbase(256*1024*1024);
	r->sizemask[0] = Winsize(256*1024*1024);
	r->sizemask[0] = Winsize(256*1024*1024);
	r->harr[0] = 0;
	r->harr[1] = 0;
}

void
xorlink(void)
{
	initport(XORE0P0REG);
	initport(XORE0P1REG);
	initport(XORE1P0REG);
	initport(XORE1P1REG);
	initengine(XORE0REG);
	initengine(XORE1REG);
	
	intrenable(Irqlo, IRQ0xor0chan0, xorintr, &xor.ports[0], "xore0p0");
	intrenable(Irqlo, IRQ0xor0chan1, xorintr, &xor.ports[0], "xore0p1");
	intrenable(Irqlo, IRQ0xor1chan0, xorintr, &xor.ports[2], "xore1p0");
	intrenable(Irqlo, IRQ0xor1chan1, xorintr, &xor.ports[2], "xore1p1");

	intrenable(Irqhi, IRQ1xor0err, xorintrerror, nil, "xorerr0");
	intrenable(Irqhi, IRQ1xor1err, xorintrerror, nil, "xorerr1");
}

void
meminitdma(uchar *buf, long n, uvlong v)
{
	Port *p;

	if(n < 128)
		panic("meminitdma with on short buffer, %ld < 128", n);

	p = portget(1);

	p->pr->cfg = ModeMeminit|Srcburst(Burst32)|Dstburst(Burst32);

	p->pr->dest = (ulong)buf;
	p->pr->blocksize = n;
	p->er->initvallo = v;
	p->er->initvalhi = v>>32;
	p->pr->act = XEstart;

	sleep(p, isdone, p);
	portput(p);
}

ulong
crc32cdma(uchar *buf, ulong n)
{
	Port *p;
	uchar descr[sizeof (Descr)+Descralign-1];
	Descr *d = (Descr*)roundup((ulong)descr, Descralign);

	if(n > 16*1024*1024)
		panic("crc32cdma on huge block");

	p = portget(0);

	p->pr->cfg = ModeCrc32c|Srcburst(Burst32)|Dstburst(Burst32);

	d->status = Sdmaowned;
	d->cmd = Ccrclast;
	d->nextdescr = 0;
	d->bytecount = n;
	d->dest = 0;
	d->src = (ulong)buf;

	p->pr->nextdescr = (ulong)d;
	p->pr->act = XEstart;

	sleep(p, isdone, p);
	assert(d->status == Ssuccess);
	portput(p);
	return d->crc32;
}

void
memdma(uchar *dst, uchar *src, ulong n)
{
	Port *p;
	uchar descr[sizeof (Descr)+Descralign-1];
	Descr *d = (Descr*)roundup((ulong)descr, Descralign);

	if(n > 16*1024*1024)
		panic("memdma on huge block");

	p = portget(0);

	p->pr->cfg = ModeDma|Srcburst(Burst32)|Dstburst(Burst32);

	d->status = Sdmaowned;
	d->cmd = 0;
	d->nextdescr = 0;
	d->bytecount = n;
	d->dest = (ulong)dst;
	d->src = (ulong)src;

	p->pr->nextdescr = (ulong)d;
	p->pr->act = XEstart;

	sleep(p, isdone, p);
	assert(d->status == Ssuccess);
	portput(p);
}

void
xordma(uchar *dst, uchar **src, int nsrc, ulong n)
{
	Port *p;
	int i;
	uchar xdescr[sizeof (XDescr)+XDescralign-1];
	XDescr *d = (XDescr*)roundup((ulong)xdescr, XDescralign);

	if(n > 16*1024*1024)
		panic("xordma on huge block");

	p = portget(0);

	p->pr->cfg = ModeXor|Srcburst(Burst32)|Dstburst(Burst32);

	d->status = Sdmaowned;
	d->cmd = MASK(nsrc);
	d->nextdescr = 0;
	d->bytecount = n;
	d->dest = (ulong)dst;
	for(i = 0; i < nsrc; i++)
		d->src[i] = (ulong)src[i];

	p->pr->nextdescr = (ulong)d;
	p->pr->act = XEstart;

	sleep(p, isdone, p);
	assert(d->status == Ssuccess);
	portput(p);
}
