#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "ureg.h"

Timer*
addclock0link(void (*clock)(void), int)
{
	/* xxx */
	return nil;
}

static void
clockintr(Ureg*, void*)
{
	rprint("T\n");
	intrclear(Irqbridge, IRQcputimer0);
}


void
clockinit(void)
{
	TimerReg *tmr = TIMERREG;

	m->ticks = 0;

	tmr->timer0 = tmr->reload0 = CLOCKFREQ/2;  /* xxx once every two seconds for now */
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
