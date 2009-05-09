#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

void
trapinit(void)
{
	IntrReg *intr = INTRREG;

	/* disable all interrupts */
	intr->lo.irqmask = ~0;
	intr->hi.irqmask = ~0;
}

void
setpanic(void)
{
	consoleprint = 1;
/*
	serwrite = uartputs;
*/
}

void
dumpstack(void)
{
}
