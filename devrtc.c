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

typedef struct Rtc	Rtc;
struct Rtc
{
	int	sec;
	int	min;
	int	hour;
	int	wday;
	int	mday;
	int	mon;
	int	year;
};

static void	setrtc(Rtc *rtc);
static ulong	rtctime(void);
static int	*yrsize(int yr);
static ulong	rtc2sec(Rtc *rtc);
static void	sec2rtc(ulong secs, Rtc *rtc);

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
		return readnum(off, buf, n, rtctime(), NUMSIZE);
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
	Rtc rtc;

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
		sec2rtc(secs, &rtc);
		setrtc(&rtc);
		return n;

	}
	error(Egreg);
	return 0;		/* not reached */
}

#define bcd2dec(bcd)	(((((bcd)>>4) & 0x0F) * 10) + ((bcd) & 0x0F))
#define dec2bcd(dec)	((((dec)/10)<<4)|((dec)%10))

enum
{
	RTCHourPM = 1<<21,	/* pm (not am) */
	RTCHour12 = 1<<22,	/* 12 hour (not 24) */
};

static ulong
rtctime(void)
{
	ulong t, d;
	Rtc rtc;

	t = RTCREG->time;
	d = RTCREG->date;

	rtc.sec = bcd2dec((t>>0) & (BITS(0, 6) - 1));
	rtc.min = bcd2dec((t>>8) & (BITS(8, 14) - 1));
		
	if(t&RTCHour12) {
		rtc.hour = bcd2dec((t>>16) & (BITS(16, 20) - 1)); /* 1 to 12 */
		if(t & RTCHourPM)
			rtc.hour += 12;
	} else {
		rtc.hour = bcd2dec((t>>16) & (BITS(16, 21) - 1)); /* 0 to 23 */
	}

	rtc.mday = bcd2dec((d>>0) & (BITS(0, 5) - 1));		/* 1 to 31 */
	rtc.mon = bcd2dec((d>>8) & (BITS(8, 12) - 1));		/* 1 to 12 */
	rtc.year = bcd2dec((d>>16) & (BITS(16, 23) - 1));	/* 00 to 99, 20xx */

	rtc.year += 2000;
	
//	print("%0.2d:%0.2d:%.02d %0.2d/%0.2d/%0.2d\n", /* HH:MM:SS YY/MM/DD */
//		rtc.hour, rtc.min, rtc.sec, rtc.year, rtc.mon, rtc.mday);

	return rtc2sec(&rtc);
}

static void
setrtc(Rtc *rtc)
{
	RTCREG->time =	(dec2bcd(rtc->hour/10)<<20) |
			(dec2bcd(rtc->hour%10)<<16) |
			(dec2bcd(rtc->min)<<8) |
			(dec2bcd(rtc->sec)<<0);

	RTCREG->date =	(dec2bcd(rtc->year - 2000)<<16) |
			(dec2bcd(rtc->mon)<<8) |
			(dec2bcd(rtc->mday)<<0);
}

#define SEC2MIN 60L
#define SEC2HOUR (60L*SEC2MIN)
#define SEC2DAY (24L*SEC2HOUR)

/*
 *  days per month plus days/year
 */
static	int	dmsize[] =
{
	365, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static	int	ldmsize[] =
{
	366, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 *  return the days/month for the given year
 */
static int *
yrsize(int yr)
{
	if((yr % 4) == 0)
		return ldmsize;
	else
		return dmsize;
}

/*
 *  compute seconds since Jan 1 1970
 */
static ulong
rtc2sec(Rtc *rtc)
{
	ulong secs;
	int i;
	int *d2m;

	secs = 0;

	/*
	 *  seconds per year
	 */
	for(i = 1970; i < rtc->year; i++){
		d2m = yrsize(i);
		secs += d2m[0] * SEC2DAY;
	}

	/*
	 *  seconds per month
	 */
	d2m = yrsize(rtc->year);
	for(i = 1; i < rtc->mon; i++)
		secs += d2m[i] * SEC2DAY;

	secs += (rtc->mday-1) * SEC2DAY;
	secs += rtc->hour * SEC2HOUR;
	secs += rtc->min * SEC2MIN;
	secs += rtc->sec;

	return secs;
}

/*
 *  compute rtc from seconds since Jan 1 1970
 */
static void
sec2rtc(ulong secs, Rtc *rtc)
{
	int d;
	long hms, day;
	int *d2m;

	/*
	 * break initial number into days
	 */
	hms = secs % SEC2DAY;
	day = secs / SEC2DAY;
	if(hms < 0) {
		hms += SEC2DAY;
		day -= 1;
	}

	/*
	 * generate hours:minutes:seconds
	 */
	rtc->sec = hms % 60;
	d = hms / 60;
	rtc->min = d % 60;
	d /= 60;
	rtc->hour = d;

	/*
	 * year number
	 */
	if(day >= 0)
		for(d = 1970; day >= *yrsize(d); d++)
			day -= *yrsize(d);
	else
		for (d = 1970; day < 0; d--)
			day += *yrsize(d-1);
	rtc->year = d;

	/*
	 * generate month
	 */
	d2m = yrsize(rtc->year);
	for(d = 1; day >= d2m[d]; d++)
		day -= d2m[d];
	rtc->mday = day + 1;
	rtc->mon = d;

	return;
}

Dev rtcdevtab = {
	'r',
	"rtc",

	devreset,
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
	devpower,
};
