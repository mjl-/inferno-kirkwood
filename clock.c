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
	Clock0link*	link;
} Clock0link;

static Clock0link *clock0link;
static Lock clock0lock;


Timer*
addclock0link(void (*clock)(void), int)
{
	Clock0link *lp;

	if((lp = malloc(sizeof(Clock0link))) == 0){
		print("addclock0link: too many links\n");
		return nil;
	}
	ilock(&clock0lock);
	lp->clock = clock;
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
			if(lp->clock)
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

	tmr->timer0 = tmr->reload0 = CLOCKFREQ/100;
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
