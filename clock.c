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

	m->ticks = 0;

	tmr->timer0 = tmr->reload0 = CLOCKFREQ/HZ;
	tmr->ctl = Tmr0enable|Tmr0periodic;

	intrenable(Irqbridge, IRQcputimer0, clockintr, nil, "timer0");
}

void
clockpoll(void)
{
}

void
clockcheck(void)
{
}
