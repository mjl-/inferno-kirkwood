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
	return nil;
}

void
clockinit(void)
{
	TimerReg *tmr = TIMERREG;

	m->ticks = 0;
	tmr->timer0 = tmr->reload0 = CLOCKFREQ/2;  /* xxx once every two seconds for now */
	tmr->ctl = Tmr0enable|Tmr0periodic;
}

void
clockpoll(void)
{
}

void
clockcheck(void)
{
}
