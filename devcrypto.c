/*
 * kirkwood crypto.
 * this is just silly test code.
 * there should probably be some integration with keyring.
 * will it involve interrupts or polling?
 * how to handle multiple users?
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#include "io.h"

enum {
	/* aes cmd */
	Aeskeymask =	(1<<2)-1,
	 Aeskey128 = 0,
	 Aeskey192,
	 Aeskey256,
	Aesmakekey =	1<<2,
	Aesinbyteswap =	1<<4,
	Aesoutbyteswap= 1<<8,
	Aeskeyready =	1<<30,
	Aestermination=	1<<31,
};


static ulong
g32(uchar *p)
{
	ulong v = 0;
	v = (v<<8)|*p++;
	v = (v<<8)|*p++;
	v = (v<<8)|*p++;
	v = (v<<8)|*p++;
	return v;
}

static void
p32(ulong v, uchar *p)
{
	*p++ = v>>24;
	*p++ = v>>16;
	*p++ = v>>8;
	*p++ = v>>0;
}

static uchar*
aesenc(void)
{
	AesReg *r = AESENCREG;
	static uchar buf[16];
	uchar key[16] = "abcdefgh01234567";

	memmove(buf, "0123456789abcdef", 16);

	while((r->cmd&Aestermination) == 0)
		;

	r->cmd = Aeskey128;
	r->key[7] = g32(key+0);
	r->key[6] = g32(key+4);
	r->key[5] = g32(key+8);
	r->key[4] = g32(key+12);

	r->data[3] = g32(buf+0);
	r->data[2] = g32(buf+4);
	r->data[1] = g32(buf+8);
	r->data[0] = g32(buf+12);

	p32(r->data[0], buf+12);
	p32(r->data[1], buf+8);
	p32(r->data[2], buf+4);
	p32(r->data[3], buf+0);

	while((r->cmd&Aestermination) == 0)
		;
	return buf;
}

enum {
	Qdir,
	Qcrypto,
};

static Dirtab cryptodir[] = {
	".",		{Qdir,0,QTDIR},	0,	0555,
	"crypto",	{Qcrypto},	0,	0666,
};

static void
cryptoreset(void)
{
	AesReg *ae = AESDECREG;

	ae->cmd = Aestermination;
}

static Chan*
cryptoattach(char *spec)
{
	return devattach('C', spec);
}

static Walkqid*
cryptowalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, cryptodir, nelem(cryptodir), devgen);
}

static int
cryptostat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, cryptodir, nelem(cryptodir), devgen);
}

static Chan*
cryptoopen(Chan *c, int omode)
{
	return devopen(c, omode, cryptodir, nelem(cryptodir), devgen);
}

static void	 
cryptoclose(Chan*)
{
}

int
min(int a, int b)
{
	return (a < b) ? a : b;
}

static long	 
cryptoread(Chan *c, void *buf, long n, vlong off)
{
	uchar *p;

	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, cryptodir, nelem(cryptodir), devgen);

	switch((ulong)c->qid.path){
	case Qcrypto:
		p = aesenc();
		n = min(16-off, n);
		memmove(buf, p+off, n);
		return n;
	}
	error(Egreg);
	return 0;		/* not reached */
}

static long	 
cryptowrite(Chan *c, void *buf, long n, vlong off)
{
	switch((ulong)c->qid.path){
	case Qcrypto:
		error(Ebadarg);
		return -1;
	}
	error(Egreg);
	return 0;		/* not reached */
}

Dev cryptodevtab = {
	'C',
	"crypto",

	cryptoreset,
	devinit,
	devshutdown,
	cryptoattach,
	cryptowalk,
	cryptostat,
	cryptoopen,
	devcreate,
	cryptoclose,
	cryptoread,
	devbread,
	cryptowrite,
	devbwrite,
	devremove,
	devwstat,
	devpower,
};
