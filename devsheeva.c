#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"


enum{
	Qdir,
	Qctl,
	Qmem,
	Qregs,

	Qdelay, // test
};

static
Dirtab sheevatab[] = {
	".",			{Qdir, 0, QTDIR},	0,	0555,
	"sheevactl",		{Qctl},			0,	0600,
	"sheevamem",		{Qmem, 0},		0,	0600,
	"sheevaregs",		{Qregs, 0},		0,	0600,
	"sheevadelay",		{Qdelay, 0},		0,	0600,
};

enum
{
	WDcheckms = 10*1000,
	WDupdatems = (WDcheckms - (WDcheckms * 5)/100),
};

/* 
 watchdog to detect hangs/halts and reset system.

 watchdogproc: periodically updates watchdog register,
 note that updates must happen before wdog checks.
*/

static void
watchdogproc(void*)
{
	//iprint("watchdogproc enabled update %ld\n", checkms);

	TIMERREG->ctl |= TmrWDenable;
	CPUCSREG->rstout |= RstoutWatchdog;

	for(;;){
		TIMERREG->timerwd = MS2TMR(WDcheckms);
		tsleep(&up->sleep, return0, nil, WDupdatems);
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

enum {
	CMwatchdog, CMcpufreq,
};
static Cmdtab sheevactl[] = 
{
	CMwatchdog,	"watchdog",	2,
	CMcpufreq,	"cpufreq",	2,
};

static long
sheevawrite(Chan* c, void* a, long n, vlong offset)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	uchar *p;
	ulong v;

	switch((ulong)c->qid.path){
	case Qctl:
		if(!iseve())
			error(Eperm);

		cb = parsecmd(a, n);
		if(waserror()) {
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, sheevactl, nelem(sheevactl));
		switch(ct->index) {
		case CMwatchdog:
			if(strcmp(cb->f[1], "on") == 0)
				TIMERREG->ctl |= TmrWDenable;
			else if(strcmp(cb->f[1], "off") == 0)
				TIMERREG->ctl &= ~TmrWDenable;
			else
				error(Ebadctl);
			break;
		case CMcpufreq:
			if(strcmp(cb->f[1], "low") == 0)
				archcpufreq(1);
			else if(strcmp(cb->f[1], "high") == 0)
				archcpufreq(0);
			else
				error(Ebadctl);
			break;
		default:
			error(Ebadctl);
			break;
		}
		poperror();
		free(cb);
		break;
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
