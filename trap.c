#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

void
trapstacks(void)
{
	setr13(PsrMfiq, m->fiqstack+nelem(m->fiqstack));
	setr13(PsrMirq, m->irqstack+nelem(m->irqstack));
	setr13(PsrMabt, m->abtstack+nelem(m->abtstack));
	setr13(PsrMund, m->undstack+nelem(m->undstack));
}

void
trapinit(void)
{
	IntrReg *intr = INTRREG;
	CpucsReg *cpucs = CPUCSREG;
	int x;

	trapstacks();
	memmove(page0->vectors, vectors, sizeof(page0->vectors));
	memmove(page0->vtable, vtable, sizeof(page0->vtable));
	/* xxx will have to flush d & i */

	/* disable all interrupts */
	intr->lo.irqmask = 0;
	intr->hi.irqmask = 0;
	cpucs->irqmask = 0;

	/* clear interrupts */
	cpucs->irq = 0;
	intr->lo.irq = 0;
	intr->hi.irq = 0;

	/* enable timer0 */
	cpucs->irqmask |= 1<<IRQcputimer0;
	intr->lo.irqmask |= 1<<IRQ0bridge;
}

void
trap(Ureg*)
{
	IntrReg *intr = INTRREG;
	CpucsReg *cpucs = CPUCSREG;

	/* xxx */
	/* must be the clock, print some bytes to uart */
	puts("t\r\n");
	
	/* reset interrupt */
	cpucs->irq = ~(1<<IRQcputimer0);
	intr->lo.irq = ~(1<<IRQ0bridge);
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
