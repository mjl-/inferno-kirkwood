#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"io.h"
#include	"interp.h"
#include	"libsec.h"

typedef struct Key Key;
struct Key {
	void	*state;
	ulong	vers;
};

static struct {
	QLock;
	Key	aesenc;
	Key	aesdec;
} crypto;

#define MASK(v)	((1<<(v))-1)
enum {
	/* aes cmd */
	Aeskeymask	= 3<<0,
	  Aeskey128 = 0,
	  Aeskey192,
	  Aeskey256,
	Aesmakekey	= 1<<2,
	Aesinbyteswap	= 1<<4,
	Aesoutbyteswap	= 1<<8,
	Aeskeyready	= 1<<30,
	Aestermination	= 1<<31,

	/* interrupt cause, irq */
	Iauthdone	= 1<<0,
	Idesdone	= 1<<1,
	Iaesencdone	= 1<<2,
	Iaesdecdone	= 1<<3,
	Iencdone	= 1<<4,
	Iacceldone	= 1<<5,
	Iacceltdmadone	= 1<<7,
	Itdmadone	= 1<<9,
	Itdmaownerr	= 1<<10,
	Iacceltdmacm	= 1<<11,

	/* window base & control */
	Winenable	= 1<<0,
#define Winbase(v)	(((v)/(64*1024))<<16)
#define	Wintarget(v)	(((v) & MASK(4))<<4)
#define Winattr(v)	(((v) & MASK(8))<<8)
#define Winsize(v)	(((v)/(64*1024)-1)<<16)

	/* accel command, cmd */
	CMDactive	= 1<<0,
	CMDdisable	= 1<<2,

	/* accel config, cfg */
	CFGstopondigesterr	= 1<<0,
	CFGwaitfortdma		= 1<<7,
	CFGactivatetdma		= 1<<9,
	CFGmultipacketchain	= 1<<11,

	/* accel status */
	STactive	= 1<<0,
	STdecodedigesterr	= 1<<8,
};


enum {
	/* sdescr config */
	SCmodemac	= 0<<0,
	SCmodecrypt	= 1<<0,
	SCmodemaccrypt	= 2<<0,
	SCmodecryptmac	= 3<<0,

	SCmacmd5	= 4<<4,
	SCmacsha1	= 5<<4,
	SCmachmacmd5	= 6<<4,
	SCmachmacsha1	= 7<<4,

	SCmacresult96	= 1<<7,

	SCcryptdes	= 1<<8,
	SCcrypt3des	= 2<<8,
	SCcryptaes	= 3<<8,

	SCencode	= 0<<12,
	SCdecode	= 1<<12,

	SCecb		= 0<<16,
	SCcbc		= 1<<16,

	SC3deseee	= 0<<20,
	SC3desede	= 1<<20,

	SCfragnone	= 0<<30,
	SCfragfirst	= 1<<30,
	SCfraglast	= 2<<30,
	SCfragmiddle	= 3<<30,
};

typedef struct SDescr SDescr;
struct SDescr
{
	ulong	cfg;
	ulong	cryptsrcdest;
	ulong	cryptbytes;
	ulong	cryptkey;
	ulong	cryptiv;
	ulong	macsrc;
	ulong	macdigest;
	ulong	maciv;
};


enum {
	Tdmaowned	= 1<<31,
	TDescralign	= 16,

	Burst32		= 3,
	Burst128	= 4,
	Tmodechained	= 0<<9,
	Tmodenonchained	= 1<<9,
	Tnobyteswap	= 1<<11,
	Tdmaenable	= 1<<12,
	Tfetchnextdescr	= 1<<13,
	Tdmaactive	= 1<<14,

#define Dstburst(v)	((v)<<0)
#define	Srcburst(v)	((v)<<6)


	Srambar		= 0x80000000,
};

typedef struct TDescr TDescr;
struct TDescr
{
	ulong	count;
	ulong	src;
	ulong	dest;
	ulong	nextdescr;
};


static struct {
	int	done;
	Rendez;
} crypt;

static struct {
	int	done;
	Rendez;
} tdma;


static int
cryptdone(void*)
{
	return crypt.done;
}

static int
tdmadone(void*)
{
	return tdma.done;
}


static ulong
g32(uchar *p)
{
	ulong v = 0;
	v = (v<<8)|*p++;
	v = (v<<8)|*p++;
	v = (v<<8)|*p++;
	v = (v<<8)|*p++;
	USED(p);
	return v;
}

static void
p32(ulong v, uchar *p)
{
	*p++ = v>>24;
	*p++ = v>>16;
	*p++ = v>>8;
	*p++ = v>>0;
	USED(p);
}

static void*
roundup(uchar *p, int n)
{
	return (void*)(((ulong)p+n-1)&~(n-1));
}


static void
cryptintr(Ureg *, void *)
{
	ulong irq;

	irq = CRYPTREG->irq;
iprint("cryptintr %#lux\n", irq);
	CRYPTREG->irq = 0;
	if(irq & Iacceltdmadone) {
		crypt.done = 1;
		wakeup(&crypt);
		return;
	}
	if(irq & Itdmadone) {
		tdma.done = 1;
		wakeup(&tdma);
		return;
	}
	if(irq & Iacceldone) {
		crypt.done = 1;
		wakeup(&crypt);
		return;
	}

	panic("crypt interrupt, accel %#lux, error %#lux\n", CRYPTREG->irq, TDMAREG->irqerr);
}

static void
crypterrintr(Ureg *, void *)
{
	panic("crypterr interrupt, %#lux\n", TDMAREG->irqerr);
}

static void
dump(char *s, uchar *p, int n)
{
	int i;
	print("%s:\n", s);
	i = 0;
	while(i < n) {
		print("%02ux", (uint)p[i]);
		if((i & 0x3) == 0x3)
			print(" ");
		if((i++ & 0xf) == 0xf)
			print("\n");
	}
	print("\n");
}

void
cesatest(void)
{
	uchar descr[3*sizeof (TDescr)+TDescralign-1];
	TDescr *d0 = roundup(descr, TDescralign);
	TDescr *d1 = d0+1;
	TDescr *d2 = d1+1;
	uchar *src, *dst;
	TdmaReg *t = TDMAREG;
	CryptReg *c = CRYPTREG;
	SDescr s;
	char key[16] = "abcdefgh01234567";
	char in[16] = "0123456789abcdef";
	char out[16];

enum {
	BASE	= 0,
	KEY	= BASE+sizeof (SDescr),
	SRC	= KEY+sizeof key,
	DST	= SRC+sizeof in,
	END	= DST+sizeof out,
	SIZE	= END-BASE,
};

	if(c->status & STactive) {
		c->cmd = CMDdisable;
		c->irq = 0;
	}
	c->irqmask = Iacceltdmadone|Itdmaownerr;

	src = smalloc(SIZE);
	dst = smalloc(SIZE);
	memset(src, 'a', SIZE);
	memset(dst, 'z', SIZE);

	s.cfg = SCmodecrypt|SCcryptaes|SCencode|SCecb|SCfragnone;
	s.cryptsrcdest = (SRC<<0)|(DST<<16);
	s.cryptbytes = sizeof in;
	s.cryptkey = KEY;
	memmove(src+BASE, &s, sizeof (SDescr));
	memmove(src+KEY, key, sizeof key);
	memmove(src+SRC, in, sizeof in);
	memset(src+DST, 0, sizeof out);

	d0->count = Tdmaowned|SIZE;
	d0->src = (ulong)src;
	d0->dest = (ulong)Srambar;
	d0->nextdescr = (ulong)d1;

	d1->count = 0;  /* cpu owned, will signal to accelerator to start */
	d1->src = 0;
	d1->dest = 0;
	d1->nextdescr = (ulong)d2;

	d2->count = Tdmaowned|SIZE;
	d2->src = (ulong)Srambar;
	d2->dest = (ulong)dst;
	d2->nextdescr = 0;

	t->nextdescr = (ulong)d0;
	t->ctl = Dstburst(Burst32)|Srcburst(Burst32)|Tmodechained|Tnobyteswap|Tfetchnextdescr; //|Tfetchnextdescr|Tdmaenable;

	c->descr = BASE;
	c->cfg = CFGstopondigesterr|CFGwaitfortdma|CFGactivatetdma;
	c->cmd = CMDactive;

	crypt.done = 0;
	t->ctl |= Tdmaenable;
	sleep(&crypt, cryptdone, nil);

	dump("src", src, SIZE);
	dump("dst", dst, SIZE);
	free(src);
	free(dst);
}

void
cesalink(void)
{
	TdmaReg *t = TDMAREG;
	CryptReg *c = CRYPTREG;

	t->addr[0].bar = Winbase(0);
	t->addr[0].winctl = Winenable|Wintarget(0x0)|Winattr(0xE)|Winsize(256*1024*1024);
	t->addr[1].bar = Winbase(256*1024*1024);
	t->addr[1].winctl = Winenable|Wintarget(0x0)|Winattr(0xD)|Winsize(256*1024*1024);
	t->addr[2].bar = Winbase(Srambar);
	t->addr[2].winctl = Winenable|Wintarget(0x3)|Winattr(0x0)|Winsize(64*1024);
	t->addr[3].bar = 0;
	t->addr[3].winctl = 0;

	t->irqerr = 0;
	t->irqerrmask = ~0;

	c->irq = 0;
	c->irqmask = ~0;

	intrenable(Irqlo, IRQ0crypto, cryptintr, nil, "crypto");
	intrenable(Irqhi, IRQ1cryptoerr, crypterrintr, nil, "cryptoerr");
}

void
setupAESstate0(AESstate *s, uchar key[], int keybytes, uchar *ivec)
{
	s->keybytes = keybytes;
	memmove(s->key, key, keybytes);
	memmove(s->ivec, ivec, sizeof (s->ivec));

	s->setup++;
}

void
aesecb(uchar *p, int n, AESstate *s, int enc)
{
	AesReg *r;
	uchar *e = p+n;
	ulong *k;
	uchar *kp, *ke;
	Key *last;

	qlock(&crypto);
	if(enc) {
		last = &crypto.aesenc;
		r = AESENCREG;
	} else {
		last = &crypto.aesdec;
		r = AESDECREG;
	}

	if(last->state != s || s->setup != last->vers) {
		if(s->keybytes == 16)
			r->cmd = Aeskey128;
		else if(s->keybytes == 24)
			r->cmd = Aeskey192;
		else if(s->keybytes == 32)
			r->cmd = Aeskey256;
		else
			errorf("bad aes keylength %d", s->keybytes);

		kp = s->key;
		ke = kp+s->keybytes;
		k = &r->key[7];
		while(kp < ke) {
			*k-- = g32(kp);
			kp += 4;
		}

		if(enc == 0) {
			/* after generating the key, we could save it for later use... */
			r->cmd |= Aesmakekey;
			while((r->cmd & Aeskeyready) == 0)
				{}
		}

		last->state = s;
		last->vers = s->setup;
	}

	while(p < e) {
		r->data[3] = g32(p+0);
		r->data[2] = g32(p+4);
		r->data[1] = g32(p+8);
		r->data[0] = g32(p+12);

		while((r->cmd & Aestermination) == 0)
			{}

		p32(r->data[0], p+12);
		p32(r->data[1], p+8);
		p32(r->data[2], p+4);
		p32(r->data[3], p+0);
		p += AESbsize;
	}
	qunlock(&crypto);
}

void
aesECBencrypt0(uchar *p, int n, AESstate *s)
{
	release();
	aesecb(p, n, s, 1);
	acquire();
}

void
aesECBdecrypt0(uchar *p, int n, AESstate *s)
{
	release();
	aesecb(p, n, s, 0);
	acquire();
}

void
aesCBCencrypt0(uchar *p, int n, AESstate *s)
{
	// xxx
	USED(p, n, s);
}

void
aesCBCdecrypt0(uchar *p, int n, AESstate *s)
{
	// xxx
	USED(p, n, s);
}
