#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"


enum{
	Qdir,
	Qmem,
	Qregs,

	Qdelay, // test
};

static
Dirtab sheevatab[]={
	".",			{Qdir, 0, QTDIR},	0,	0555,
	"sheevamem",		{Qmem, 0},		0,	0600,
	"sheevaregs",		{Qregs, 0},		0,	0600,
	"sheevadelay",		{Qdelay, 0},		0,	0600,
};

/* 
 watchdog to detect hangs/halts and reset system.

 watchdogproc: periodically updates watchdog register,
 note that updates must happen before wdog checks.
*/

static void
watchdogproc(void*)
{
	int checkms = 10*1000;
	int updatems = (checkms - (checkms * 5)/100);

	//iprint("watchdogproc enabled update %ld\n", checkms);

	TIMERREG->ctl |= TmrWDenable;
	CPUCSREG->rstout |= RstoutWatchdog;

	for(;;){
		TIMERREG->timerwd = MS2TMR(checkms);
		tsleep(&up->sleep, return0, nil, updatems);
	}
}

static void
sheevainit(void)
{
	kproc("watchdog", watchdogproc, nil, 0);
}

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
	case Qdelay:
		v = atoi(a);
		delay(v);
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
	sheevainit,
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
