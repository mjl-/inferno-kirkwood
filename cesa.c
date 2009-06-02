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

static QLock hashlock;

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

	/* hash cmd */
	CMDmd5		= 0<<0,
	CMDsha1		= 1<<0,
	CMDcontinue	= 1<<1,
	CMDbyteswap	= 1<<2,
	CMDivbyteswap	= 1<<4,
	CMDdone		= 1<<31,

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
	v |= *p++<<24;
	v |= *p++<<16;
	v |= *p++<<8;
	v |= *p++<<0;
	USED(p);
	return v;
}

static ulong
g32le(uchar *p)
{
	ulong v = 0;
	v |= *p++<<0;
	v |= *p++<<8;
	v |= *p++<<16;
	v |= *p++<<24;
	USED(p);
	return v;
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
p32le(uchar *p, ulong v)
{
	*p++ = v>>0;
	*p++ = v>>8;
	*p++ = v>>16;
	*p++ = v>>24;
	USED(p);
}

static ulong
swap(ulong v)
{
	uchar buf[4];
	p32(buf, v);
	return g32le(buf);
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
	c->irqmask = Iacceltdmadone|Itdmaownerr|Iacceltdmacm;

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

		p32(p+12, r->data[0]);
		p32(p+8, r->data[1]);
		p32(p+4, r->data[2]);
		p32(p+0, r->data[3]);
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


typedef struct Buf Buf;
struct Buf
{
	uchar	*p;
	ulong	n;
	int	finish;
};

static void
hashdma(Buf *bufs, int nbufs, DigestState *s, uchar *digest, int sha1)
{
	USED(bufs, nbufs, s, digest, sha1);
	panic("hashdma, xxx");
}


static void
hashfinish(ulong cmd, uchar *p, int n, uchar *digest, int sha1, uvlong nb)
{
	HashReg *r = HASHREG;
	int i;
	uchar *e;
	ulong v;

	if(n+1 > 56) {
		/* last block with data, but bitcount doesn't fit and needs another */
		r->cmd = cmd;
		for(e = p+(n & ~(4-1)); p < e; p += 4)
			r->data = g32(p);
		v = 0;
		for(i = 0; i < n % 4; i++)
			v |= (ulong)*p++ << ((4-1-i)*8);
		v |= 0x80<<((4-1-i)*8);
		r->data = v;
		if(n+1 <= 60)
			r->data = 0;

		cmd |= CMDcontinue;
		while((r->cmd & CMDdone) == 0)
			{}

		/* last block with just zeros */
		r->cmd = cmd;
	} else {
		/* last block with data & bitcount */
		r->cmd = cmd;
		for(e = p+(n & ~(4-1)); p < e; p += 4)
			r->data = g32(p);
		v = 0;
		for(i = 0; i < n % 4; i++)
			v |= (ulong)*p++ << ((4-1-i)*8);
		v |= 0x80<<((4-1-i)*8);
		r->data = v;
	}

	/* zero-fill the last block by writing the bitcount */
	if(sha1) {
		r->bitcountlo = nb>>32;
		r->bitcounthi = nb>>0;
	} else {
		/* r->cmd byteswap apparently ignores bitcount words... */
		r->bitcountlo = swap(nb>>0);
		r->bitcounthi = swap(nb>>32);
	}
	while((r->cmd & CMDdone) == 0)
		{}
	if(sha1)
		for(i = 0; i < 5; i++)
			p32(digest+i*4, r->iv[i]);
	else
		for(i = 0; i < 4; i++)
			p32le(digest+i*4, r->iv[i]);
}

static void
hashexec(Buf *bufs, int nbufs, DigestState *s, uchar *digest, int sha1)
{
	HashReg *r = HASHREG;
	Buf *b;
	uchar *p, *e;
	int i, n, nw;
	ulong cmd;

	qlock(&hashlock);

	cmd = sha1 ? CMDsha1 : (CMDmd5|CMDbyteswap);
	nw = sha1 ? 5 : 4;
	if(s->seeded) {
		cmd |= CMDcontinue;
		for(i = 0; i < nw; i++)
			r->iv[i] = s->state[i];
	}
	s->seeded = 1;

	for(i = 0; i < nbufs && !bufs[i].finish; i++) {
		b = &bufs[i];
		p = b->p;
		n = b->n;

		while(n >= 64) {
			r->cmd = cmd;
			cmd |= CMDcontinue;
			for(e = p+64; p < e; p += 4)
				r->data = g32(p);
			while((r->cmd & CMDdone) == 0)
				{}
			n -= 64;
			p = e;
		}
	}

	if(i < nbufs && bufs[i].finish) {
		b = &bufs[i];
		hashfinish(cmd, b->p, b->n, digest, sha1, s->len*8);
	} else {
		for(i = 0; i < nw; i++)
			s->state[i] = r->iv[i];
	}

	qunlock(&hashlock);
}

DigestState*
hash(uchar *p, ulong len, uchar *digest, DigestState *s, int sha1)
{
	Buf bufs[3];
	Buf *b;
	int nbufs;
	int nblocks;
	int n;

	release();

	if(s == nil) {
		s = smalloc(sizeof s[0]);
		s->len = 0;
		s->blen = 0;
		s->seeded = 0;
	}
	s->len += len;
	nbufs = 0;
	nblocks = 0;

	if(s->blen > 0) {
		n = 64-s->blen;
		if(n > len)
			n = len;
		memmove(s->buf+s->blen, p, n);
		s->blen += n;
		p += n;
		len -= n;
		if(s->blen == 64) {
			b = &bufs[nbufs++];
			b->p = s->buf;
			b->n = 64;
			b->finish = 0;
			nblocks++;
			s->blen = 0;
		}
	}

	if(len >= 64) {
		b = &bufs[nbufs++];
		b->p = p;
		b->n = len & ~(64-1);
		nblocks += b->n/64;
		b->finish = 0;
	}
	len %= 64;

	if(digest != nil) {
		b = &bufs[nbufs++];
		b->finish = 1;
		if(len > 0) {
			b->p = p;
			b->n = len;
		} else {
			b->p = s->buf;
			b->n = s->blen;
		}
		nblocks++;
	}

	if(nblocks > 0) {
		if(nblocks > 64 || 1)
			hashexec(bufs, nbufs, s, digest, sha1);
		else
			hashdma(bufs, nbufs, s, digest, sha1);
	}

	if(digest == nil && len > 0) {
		memmove(s->buf, p, len);
		s->blen = len;
	}

	acquire();

	return s;
}

DigestState*
sha1(uchar *p, ulong len, uchar *digest, DigestState *s)
{
	return hash(p, len, digest, s, 1);
}

DigestState*
md5(uchar *p, ulong len, uchar *digest, DigestState *s)
{
	return hash(p, len, digest, s, 0);
}
