#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"


enum{
	Qdir,
	Qmem,
	Qregs,
};

static
Dirtab sheevatab[]={
	".",			{Qdir, 0, QTDIR},	0,	0555,
	"sheevamem",		{Qmem, 0},		0,	0600,
	"sheevaregs",		{Qregs, 0},		0,	0600,
};

static Chan*
sheevaattach(char* spec)
{
	return devattach('T', spec);
}

static Walkqid*
sheevawalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, sheevatab, nelem(sheevatab), devgen);
}

static int
sheevastat(Chan* c, uchar *db, int n)
{
	return devstat(c, db, n, sheevatab, nelem(sheevatab), devgen);
}

static Chan*
sheevaopen(Chan* c, int omode)
{
	return devopen(c, omode, sheevatab, nelem(sheevatab), devgen);
}


static void
sheevaclose(Chan* c)
{
	USED(c);
}

static long
sheevaread(Chan* c, void* a, long n, vlong offset)
{
	uchar *p;
	ulong v;

	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, sheevatab, nelem(sheevatab), devgen);
	case Qmem:
		memmove(a, (void*)offset, n);
		break;
	case Qregs:
		if(n != 4 || offset % 4 != 0)
			error(Ebadusefd);
		p = a;
		v = *(ulong*)offset;
		*p++ = v>>0;
		*p++ = v>>8;
		*p++ = v>>16;
		*p++ = v>>24;
		USED(p);
		break;
	default:
		n=0;
		break;
	}
	return n;
}

static long
sheevawrite(Chan* c, void* a, long n, vlong offset)
{
	uchar *p;
	ulong v;

	switch((ulong)c->qid.path){
	case Qmem:
		memmove((void*)offset, a, n);
		break;
	case Qregs:
		if(n != 4 || offset % 4 != 0)
			error(Ebadusefd);
		p = a;
		v = 0;
		v |= p[0]<<0;
		v |= p[1]<<8;
		v |= p[2]<<16;
		v |= p[3]<<24;
		*(ulong*)offset = v;
		break;
	default:
		error(Ebadusefd);
	}
	return n;
}

Dev sheevadevtab = {
	'T',
	"sheeva",

	devreset,
	devinit,
	devshutdown,
	sheevaattach,
	sheevawalk,
	sheevastat,
	sheevaopen,
	devcreate,
	sheevaclose,
	sheevaread,
	devbread,
	sheevawrite,
	devbwrite,
	devremove,
	devwstat,
};
