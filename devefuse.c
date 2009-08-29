#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

/*
 * WARNING: writing to the efuses is untested.
 * we might have to manually handle the voltage requirements?
 */

enum {
	/* protection */
	Psecurity0	= 1<<0,
	Psecurity1	= 1<<1,

	/* ctl */
	Cburn		= 1<<0,
	Cwrite0		= 1<<1,
	Cwrite1		= 1<<2,
	Cburndone	= 1<<16,
};

static QLock efuselock;

enum {
	Qdir,
	Qefuse0,
	Qefuse1,
};

static
Dirtab efusetab[]={
	".",		{Qdir, 0, QTDIR},	0,	0555,
	"efuse0",	{Qefuse0, 0},		8,	0666,
	"efuse1",	{Qefuse1, 0},		8,	0666,
};

static Chan*
efuseattach(char* spec)
{
	return devattach(L'φ', spec);
}

static Walkqid*
efusewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, efusetab, nelem(efusetab), devgen);
}

static int
efusestat(Chan* c, uchar *db, int n)
{
	return devstat(c, db, n, efusetab, nelem(efusetab), devgen);
}

static Chan*
efuseopen(Chan* c, int omode)
{
	return devopen(c, omode, efusetab, nelem(efusetab), devgen);
}

static void
efuseclose(Chan* c)
{
	USED(c);
}

#define max(a, b) ((a)>(b) ? (a): (b))

static void
p32(uchar *p, ulong v)
{
	*p++ = v>>24;
	*p++ = v>>16;
	*p++ = v>>8;
	*p++ = v>>0;
	USED(p);
}

static ulong
g32(uchar *p)
{
	ulong v;
	v = 0;
	v |= (ulong)*p++<<24;
	v |= (ulong)*p++<<16;
	v |= (ulong)*p++<<8;
	v |= (ulong)*p++<<0;
	USED(p);
	return v;
}

static long
efuseread(Chan* c, void* a, long n, vlong offset)
{
	uchar buf[8];
	EfuseReg *reg = EFUSEREG;

	qlock(&efuselock);
	if(waserror()) {
		qunlock(&efuselock);
		nexterror();
	}

	switch((ulong)c->qid.path){
	case Qdir:
		n = devdirread(c, a, n, efusetab, nelem(efusetab), devgen);
		break;
	case Qefuse0:
		p32(buf+4, reg->lo0);
		p32(buf+0, reg->hi0);
		n = max(0, sizeof buf-(long)offset);
		memmove(a, buf+offset, n);
		break;
	case Qefuse1:
		p32(buf+4, reg->lo1);
		p32(buf+0, reg->hi1);
		n = max(0, sizeof buf-(long)offset);
		memmove(a, buf+offset, n);
		break;
	default:
		n=0;
		break;
	}

	qunlock(&efuselock);
	poperror();

	return n;
}

static void
efwrite(ulong *lo, ulong *hi, int secbit, int writebit, uchar *p, long n, vlong offset)
{
	EfuseReg *reg = EFUSEREG;
	int i;

	if(offset != 0 || n != 8)
		error(Ebadarg);

	if(reg->ctl & Cburn)
		error("burn mode already active");

	if(reg->protection & secbit)
		error("efuse already burned");

	reg->ctl |= Cburn;
	reg->protection |= secbit;
	*lo = g32(p+4);
	*hi = g32(p+0);
	reg->ctl |= writebit;

	i = 0;
	while((reg->ctl & Cburndone) == 0) {
		if(i++ >= 500) {
			reg->ctl = 0;
			error("burning timed out");
		}
		tsleep(&up->sleep, return0, nil, 2);
	}
	reg->ctl = 0;
}

static long
efusewrite(Chan* c, void* a, long n, vlong offset)
{
	EfuseReg *reg = EFUSEREG;

	qlock(&efuselock);
	if(waserror()) {
		qunlock(&efuselock);
		nexterror();
	}

	switch((ulong)c->qid.path){
	case Qefuse0:
		efwrite(&reg->lo0, &reg->hi0, Psecurity0, Cwrite0, a, n, offset);
		break;
	case Qefuse1:
		efwrite(&reg->lo1, &reg->hi1, Psecurity1, Cwrite1, a, n, offset);
		break;
	default:
		error(Ebadusefd);
	}

	qunlock(&efuselock);
	poperror();

	return n;
}

Dev efusedevtab = {
	L'φ',
	"efuse",

	devreset,
	devinit,
	devshutdown,
	efuseattach,
	efusewalk,
	efusestat,
	efuseopen,
	devcreate,
	efuseclose,
	efuseread,
	devbread,
	efusewrite,
	devbwrite,
	devremove,
	devwstat,
};
