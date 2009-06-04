#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "ureg.h"

typedef struct Clock0link Clock0link;
typedef struct Clock0link {
	void		(*clock)(void);
	ulong		tm;
	Clock0link*	link;
} Clock0link;

static Clock0link *clock0link;
static Lock clock0lock;


Timer*
addclock0link(void (*clock)(void), int ticks)
{
	Clock0link *lp;

	if((lp = malloc(sizeof(Clock0link))) == 0){
		print("addclock0link: too many links\n");
		return nil;
	}
	ilock(&clock0lock);
	lp->clock = clock;
	lp->tm = ticks;
	lp->link = clock0link;
	clock0link = lp;
	iunlock(&clock0lock);
	return nil;
}

static void
clockintr(Ureg*, void*)
{
	Clock0link *lp;

	m->ticks++;

	checkalarms();

	if(canlock(&clock0lock)){
		for(lp = clock0link; lp; lp = lp->link)
			if(m->ticks % lp->tm == 0)
				lp->clock();
		unlock(&clock0lock);
	}

	intrclear(Irqbridge, IRQcputimer0);
}


void
clockinit(void)
{
	TimerReg *tmr = TIMERREG;

	/* TODO adjust m->bootdelay, used by delay() */

	m->ticks = 0;

	tmr->timer0 = tmr->reload0 = CLOCKFREQ/HZ;
	tmr->ctl = Tmr0enable|Tmr0periodic;

	intrenable(Irqbridge, IRQcputimer0, clockintr, nil, "timer0");
}


uvlong
fastticks(uvlong *hz)
{
	if(hz)
		*hz = HZ;
	return m->ticks;
}

void
microdelay(int l)
{
	int i;

	l *= m->delayloop;
	l /= 1000;
	if(l <= 0)
		l = 1;
	for(i = 0; i < l; i++)
		{}
}

void
delay(int l)
{
	ulong i, j;

	j = m->delayloop;
	while(l-- > 0)
		for(i=0; i < j; i++)
			{}
}


void
clockpoll(void)
{
}

void
clockcheck(void)
{
}
