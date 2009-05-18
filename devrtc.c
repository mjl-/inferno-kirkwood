#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"io.h"

enum {
	Qdir,
	Qrtc,
};

static Dirtab rtcdir[] = {
	".",		{Qdir,0,QTDIR},	0,	0555,
	"rtc",		{Qrtc},	NUMSIZE,	0664,
};

extern ulong boottime;

static void
rtcreset(void)
{
}

static ulong
getbcd(ulong bcd)
{
	return (bcd&0x0f) + 10 * (bcd>>4);
}

static ulong
putbcd(ulong val)
{
	return (val % 10) | (((val/10) % 10) << 4);
}

static int
isleap(int y)
{
	return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

static int
yeardays(int year)
{
	return isleap(year) ? 366 : 365;
}

int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static int
monthdays(int year, int mon)
{
	return days[mon] + ((mon == 1 && isleap(year)) ? 1 : 0);
}


#define MASK(x)	((1<<(x))-1)
static ulong
rtctimeget(void)
{
	int i, nd;
	ulong s, m, h, day, mon, year, v;
	ulong t = RTCREG->time;
	ulong d = RTCREG->date;

	s = getbcd((t>>0)&MASK(7));
	m = getbcd((t>>8)&MASK(7));
		
	if(t&(1<<22)) {
		/* am/pm mode is used */
		h = getbcd((t>>16)&MASK(5));
		if(t&(1<<21))
			h += 12;	/* pm (not am) */
	} else {
		h = getbcd((t>>16)&MASK(6));
	}

	day = getbcd((d>>0)&MASK(6));	/* 1 to 31 */
	mon = getbcd((d>>8)&MASK(5));	/* 1 to 12 */
	year = getbcd((d>>16)&MASK(8));	/* 00 to 99, 20xx */

	year += 2000;
	day -= 1;
	mon -= 1;

	v = 0;
	for(i = 1970; i < year; i++)
		day += yeardays(i);
	for(i = 0; i < mon; i++)
		day += monthdays(year, mon);
	v += day*24*3600;
	v += h*3600+m*60+s;

	return v;
}

static void
rtctimeset(ulong v)
{
	ulong n, year, mon, day, h, m, s;

	/* we start at unix epoch, and keep adding years,months,days,hours,minutes,seconds until we match parameter v */
	n = 0;

	for(year = 1970; n+(yeardays(year)*24*3600) <= v; year++)
		n += yeardays(year)*24*3600;
	for(mon = 0; n+(monthdays(year, mon)*24*3600) <= v; mon++)
		n += monthdays(year, mon)*24*3600;
	for(day = 0; n+24*3600 <= v; day++)
		n += 24*3600;

	for(h = 0; n+3600 <= v; h++)
		n += 3600;
	for(m = 0; n+60 <= v; m++)
		n += 60;
	s = v-n;

	mon += 1;
	day += 1;
	year %= 2000;

	RTCREG->time = (putbcd(h/10)<<20) | (putbcd(h%10)<<16) | (putbcd(m)<<8) | (putbcd(s)<<0);
	RTCREG->date = (putbcd(year)<<16) | (putbcd(mon)<<8) | (putbcd(day)<<0);
}


static Chan*
rtcattach(char *spec)
{
	return devattach('r', spec);
}

static Walkqid*
rtcwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, rtcdir, nelem(rtcdir), devgen);
}

static int
rtcstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, rtcdir, nelem(rtcdir), devgen);
}

static Chan*
rtcopen(Chan *c, int omode)
{
	return devopen(c, omode, rtcdir, nelem(rtcdir), devgen);
}

static void	 
rtcclose(Chan*)
{
}

static long	 
rtcread(Chan *c, void *buf, long n, vlong off)
{
	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, rtcdir, nelem(rtcdir), devgen);

	switch((ulong)c->qid.path){
	case Qrtc:
		return readnum(off, buf, n, rtctimeget(), NUMSIZE);
	}
	error(Egreg);
	return 0;		/* not reached */
}

static long	 
rtcwrite(Chan *c, void *buf, long n, vlong off)
{
	ulong offset = off;
	ulong secs;
	char *cp, sbuf[32];

	switch((ulong)c->qid.path){
	case Qrtc:
		if(offset != 0 || n >= sizeof(sbuf)-1)
			error(Ebadarg);
		memmove(sbuf, buf, n);
		sbuf[n] = '\0';
		cp = sbuf;
		while(*cp){
			if(*cp>='0' && *cp<='9')
				break;
			cp++;
		}
		secs = strtoul(cp, 0, 0);
		rtctimeset(secs);
		return n;

	}
	error(Egreg);
	return 0;		/* not reached */
}

static void
rtcpower(int on)
{
	if(on)
		boottime = rtctimeget() - TK2SEC(MACHP(0)->ticks);
}

Dev rtcdevtab = {
	'r',
	"rtc",

	rtcreset,
	devinit,
	devshutdown,
	rtcattach,
	rtcwalk,
	rtcstat,
	rtcopen,
	devcreate,
	rtcclose,
	rtcread,
	devbread,
	rtcwrite,
	devbwrite,
	devremove,
	devwstat,
	rtcpower,
};
