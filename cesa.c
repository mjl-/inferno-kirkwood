#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"io.h"
#include	"interp.h"
#include	"libsec.h"

/*
 * todo:
 * - use security accelerator+tdma
 * - zero-pad blocks, at least in ecb.  perhaps in cbc too?
 * - set up decryption key at "aes setup" time, only once?
 * - treat aes encryption & decryption engines separately?
 * - enable des acceleration.  it's off now because inferno uses
 *   functions (e.g. block_cipher) that directly use the expanded key.
 */

static QLock hashlock;
static QLock aeslock;
static QLock deslock;
static QLock accellock;


#define MASK(v)	((1<<(v))-1)
enum {
	/* aes cmd */
	AESkeymask	= 3<<0,
	AESkey128	= 0<<0,
	AESkey192	= 1<<0,
	AESkey256	= 2<<0,
	AESmakekey	= 1<<2,
	AESinbyteswap	= 1<<4,
	AESoutbyteswap	= 1<<8,
	AESkeyready	= 1<<30,
	AESdone		= 1<<31,

	/* des cmd */
	DESencrypt	= 0<<0,
	DESdecrypt	= 1<<0,
	DEStriple	= 1<<1,
	DESeee		= 0<<2,
	DESede		= 1<<2,
	DESecb		= 0<<3,
	DEScbc		= 1<<3,
	DESbyteswap	= 1<<4,
	DESivbyteswap	= 1<<6,
	DESoutbyteswap	= 1<<8,
	DESwriteallow	= 1<<29,
	DESdoneall	= 1<<30,
	DESdonesingle	= 1<<31,

	/* hash cmd */
	HASHmd5		= 0<<0,
	HASHsha1	= 1<<0,
	HASHcontinue	= 1<<1,
	HASHbyteswap	= 1<<2,
	HASHivbyteswap	= 1<<4,
	HASHdone	= 1<<31,

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
	ACactive	= 1<<0,
	ACdisable	= 1<<2,

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


static int
cryptdone(void*)
{
	return crypt.done;
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
		c->cmd = ACdisable;
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
	c->cmd = ACactive;

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
setupAESstate(AESstate *s, uchar *key, int keybytes, uchar *ivec)
{
	s->keybytes = keybytes;
	memmove(s->key, key, keybytes);
	memmove(s->ivec, ivec, sizeof (s->ivec));
}

/* xxx should zero-pad aes ecb encryption blocks */
void
aes(uchar *p, int n, AESstate *s, int enc, int cbc)
{
	AesReg *r;
	uchar *e = p+n;
	ulong *k;
	uchar *kp, *ke;
	ulong v[4], w[4], *a, *b, *t;
	ulong cmd = 0;

	if(s->keybytes == 16)
		cmd = AESkey128;
	else if(s->keybytes == 24)
		cmd = AESkey192;
	else if(s->keybytes == 32)
		cmd = AESkey256;
	else
		errorf("bad aes keylength %d", s->keybytes);

	qlock(&aeslock);
	r = enc ? AESENCREG : AESDECREG;
	r->cmd = cmd;

	kp = s->key;
	ke = kp+s->keybytes;
	k = &r->key[7];
	while(kp < ke) {
		*k-- = g32(kp);
		kp += 4;
	}

	if(!enc) {
		/* xxx we should save this for later use */
		r->cmd |= AESmakekey;
		while((r->cmd & AESkeyready) == 0)
			{}
	}

	if(cbc) {
		v[0] = g32(s->ivec+0);
		v[1] = g32(s->ivec+4);
		v[2] = g32(s->ivec+8);
		v[3] = g32(s->ivec+12);
		if(enc) {
			while(p < e) {
				r->data[3] = g32(p+0)^v[0];
				r->data[2] = g32(p+4)^v[1];
				r->data[1] = g32(p+8)^v[2];
				r->data[0] = g32(p+12)^v[3];

				while((r->cmd & AESdone) == 0)
					{}

				p32(p+0,  v[0]=r->data[3]);
				p32(p+4,  v[1]=r->data[2]);
				p32(p+8,  v[2]=r->data[1]);
				p32(p+12, v[3]=r->data[0]);
				p += AESbsize;
			}
			b = v;
		} else {
			a = v;
			b = w;
			while(p < e) {
				r->data[3] = b[0]=g32(p+0);
				r->data[2] = b[1]=g32(p+4);
				r->data[1] = b[2]=g32(p+8);
				r->data[0] = b[3]=g32(p+12);

				while((r->cmd & AESdone) == 0)
					{}

				p32(p+0,  a[0]^r->data[3]);
				p32(p+4,  a[1]^r->data[2]);
				p32(p+8,  a[2]^r->data[1]);
				p32(p+12, a[3]^r->data[0]);
				p += AESbsize;
				t = a;
				a = b;
				b = t;
			}
		}
		p32(s->ivec+0, b[0]);
		p32(s->ivec+4, b[1]);
		p32(s->ivec+8, b[2]);
		p32(s->ivec+12, b[3]);
	} else
		while(p < e) {
			r->data[3] = g32(p+0);
			r->data[2] = g32(p+4);
			r->data[1] = g32(p+8);
			r->data[0] = g32(p+12);

			while((r->cmd & AESdone) == 0)
				{}

			p32(p+12, r->data[0]);
			p32(p+8, r->data[1]);
			p32(p+4, r->data[2]);
			p32(p+0, r->data[3]);
			p += AESbsize;
		}
	qunlock(&aeslock);
}


void
aesECBencrypt(uchar *p, int n, AESstate *s)
{
	release();
	aes(p, n, s, 1, 0);
	acquire();
}

void
aesECBdecrypt(uchar *p, int n, AESstate *s)
{
	release();
	aes(p, n, s, 0, 0);
	acquire();
}

void
aesCBCencrypt(uchar *p, int n, AESstate *s)
{
	release();
	aes(p, n, s, 1, 1);
	acquire();
}

void
aesCBCdecrypt(uchar *p, int n, AESstate *s)
{
	release();
	aes(p, n, s, 0, 1);
	acquire();
}


/*
 * the des engine is disabled because libinterp/keyring.c and other,
 * in-kernel code all call block_cipher or triple_block_cipher.
 * we cannot as easily override those functions.
 *
 * xxx triple des hasn't been tested (it isn't exported to limbo)
 * xxx we should pad partial blocks with zero bytes, at least for ecb.
 */
static void
des(uchar *p, int n, uchar *key, uchar *ivec, int enc, int triple, int eee)
{
	DesReg *r = DESREG;
	uchar *e, *q;
	ulong cmd;

	if(n == 0)
		return;

	qlock(&deslock);
	cmd = DESwriteallow;
	cmd |= enc ? DESencrypt : DESdecrypt;
	cmd |= (ivec != nil) ? DEScbc : DESecb;
	cmd |= triple ? DEStriple : 0;
	cmd |= eee ? DESeee : DESede;
	r->cmd = cmd;

	r->key0lo = g32(key+4);
	r->key0hi = g32(key+0);
	if(triple) {
		r->key1lo = g32(key+12);
		r->key1hi = g32(key+8);
		r->key2lo = g32(key+20);
		r->key2hi = g32(key+16);
	}
	if(ivec != nil) {
		r->ivlo = g32(ivec+4);
		r->ivhi = g32(ivec+0);
	}

	e = p+n;
	q = p;

	/*
	 * we use the two-entry pipeline, start with writing the first block.
	 * the engine starts after writing to datahi.
	 */
	r->datalo = g32(p+4);
	r->datahi = g32(p+0);
	p += 8;

	while(p < e) {
		while((r->cmd & DESwriteallow) == 0)
			{}
		r->datalo = g32(p+4);
		r->datahi = g32(p+0);
		p += 8;

		/* we must read from hi first, then lo */
		p32(q+0, r->outhi);
		p32(q+4, r->outlo);
		q += 8;
	}

	/* read remaining block from pipeline */
	while((r->cmd & DESdonesingle) == 0)
		{}
	p32(q+0, r->outhi);
	p32(q+4, r->outlo);

	if(ivec != nil) {
		p32(ivec+4, r->ivlo);
		p32(ivec+0, r->ivhi);
	}

	qunlock(&deslock);
}

void
setupDESstate0(DESstate *s, uchar key[8], uchar *ivec)
{
	memmove(s->key, key, sizeof s->key);
	if(ivec != nil)
		memmove(s->ivec, ivec, sizeof s->ivec);
}

void
desECBencrypt0(uchar *p, int n, DESstate *s)
{
	des(p, n, s->key, nil, 1, 0, 0);
}

void
desECBdecrypt0(uchar *p, int n, DESstate *s)
{
	des(p, n, s->key, nil, 0, 0, 0);
}

void
desCBCencrypt0(uchar *p, int n, DESstate *s)
{
	des(p, n, s->key, s->ivec, 1, 0, 0);
}

void
desCBCdecrypt0(uchar *p, int n, DESstate *s)
{
	des(p, n, s->key, s->ivec, 0, 0, 0);
}

void
setupDES3state0(DES3state *s, uchar key[3][8], uchar *ivec)
{
	memmove(s->key, key, sizeof s->key);
	if(ivec != nil)
		memmove(s->ivec, ivec, sizeof s->ivec);
}

void
des3ECBencrypt0(uchar *p, int n, DES3state *s)
{
	des(p, n, (uchar*)s->key, nil, 1, 1, 0);
}

void
des3ECBdecrypt0(uchar *p, int n, DES3state *s)
{
	des(p, n, (uchar*)s->key, nil, 0, 1, 0);
}

void
des3CBCencrypt0(uchar *p, int n, DES3state *s)
{
	des(p, n, (uchar*)s->key, s->ivec, 1, 1, 0);
}

void
des3CBCdecrypt0(uchar *p, int n, DES3state *s)
{
	des(p, n, (uchar*)s->key, s->ivec, 0, 1, 0);
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

		cmd |= HASHcontinue;
		while((r->cmd & HASHdone) == 0)
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
	while((r->cmd & HASHdone) == 0)
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

	cmd = sha1 ? HASHsha1 : (HASHmd5|HASHbyteswap);
	nw = sha1 ? 5 : 4;
	if(s->seeded) {
		cmd |= HASHcontinue;
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
			cmd |= HASHcontinue;
			for(e = p+64; p < e; p += 4)
				r->data = g32(p);
			while((r->cmd & HASHdone) == 0)
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
