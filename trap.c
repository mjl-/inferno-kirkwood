#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#define waslo(sr) (!((sr) & (PsrDirq|PsrDfiq)))


Instr BREAK = 0xE6BAD010;

int (*breakhandler)(Ureg*, Proc*);
int (*catchdbg)(Ureg *, uint);


typedef struct Handler Handler;
struct Handler {
	void	(*r)(Ureg*, void*);
	void	*a;
	char	name[KNAMELEN];
};

static Handler irqlo[32];
static Handler irqhi[32];
static Handler irqbridge[32];
static Lock irqlock;

typedef struct Irq Irq;
struct Irq {
	ulong	*irq;
	ulong	*irqmask;
	Handler	*irqvec;
	int	nirqvec;
	char	*name;
};
static Irq irqs[] = {
[Irqlo]		{&INTRREG->lo.irq,	&INTRREG->lo.irqmask,	irqlo, nelem(irqlo),		"lo"},
[Irqhi]		{&INTRREG->hi.irq,	&INTRREG->hi.irqmask,	irqhi, nelem(irqhi),		"hi"},
[Irqbridge]	{&CPUCSREG->irq,	&CPUCSREG->irqmask,	irqbridge, nelem(irqbridge),	"bridge"},
};

void
intrclear(int sort, int v)
{
	*irqs[sort].irq = ~(1<<v);
}

void
intrmask(int sort, int v)
{
	*irqs[sort].irqmask &= ~(1<<v);
}

void
intrunmask(int sort, int v)
{
	*irqs[sort].irqmask |= (1<<v);
}

static void
maskallints(void)
{
	/* no fiq or ep in use */
	INTRREG->lo.irqmask = 0;
	INTRREG->hi.irqmask = 0;
	CPUCSREG->irqmask = 0;
}


void
intrset(Handler *h, void (*f)(Ureg*, void*), void *a, char *name)
{
	if(h->r != nil)
		panic("duplicate irq: %s (%#p)\n", h->name, h->r);
	h->r = f;
	h->a = a;
	strncpy(h->name, name, KNAMELEN-1);
	h->name[KNAMELEN-1] = 0;
}

void
intrunset(Handler *h)
{
	h->r = nil;
	h->a = nil;
	h->name[0] = 0;
}

void
intrdel(Handler *h, void (*f)(Ureg*, void*), void *a, char *name)
{
	if(h->r != f || h->a != a || strcmp(h->name, name) != 0)
		return;
	intrunset(h);
}

void
intrenable(int sort, int v, void (*f)(Ureg*, void*), void *a, char *name)
{
	int x;

	ilock(&irqlock);
	intrset(&irqs[sort].irqvec[v], f, a, name);
	x = splhi();
	intrunmask(sort, v);
	splx(x);
	iunlock(&irqlock);
}

void
intrdisable(int sort, int v, void (*f)(Ureg*, void*), void* a, char *name)
{
	int x;

	ilock(&irqlock);
	intrdel(&irqs[sort].irqvec[v], f, a, name);
	x = splhi();
	intrmask(sort, v);
	splx(x);
	iunlock(&irqlock);
}

static void
intrs(Ureg *ur, int sort)
{
	Handler *h;
	int i, s;
	ulong ibits;
	Irq irq;

	irq = irqs[sort];
	ibits = *irq.irq;
	ibits &= *irq.irqmask;

	for(i = 0; i < irq.nirqvec && ibits; i++)
		if(ibits & (1<<i)){
			h = &irq.irqvec[i];
			if(h->r != nil){
				h->r(ur, h->a);
				ibits &= ~(1<<i);
			}
		}
	if(ibits != 0) {
		iprint("spurious irq%s interrupt: %8.8lux\n", irq.name, ibits);
		s = splfhi();
		*irq.irq &= ibits;
		splx(s);
	}
}

void
intrhi(Ureg *ureg, void*)
{
	intrs(ureg, Irqhi);
}

void
intrbridge(Ureg *ureg, void*)
{
	intrs(ureg, Irqbridge);
	intrclear(Irqlo, IRQ0bridge);
}


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
	int i;

	trapstacks();
	memmove(page0->vectors, vectors, sizeof(page0->vectors));
	memmove(page0->vtable, vtable, sizeof(page0->vtable));
	/* xxx will have to flush d & i */

	for(i = 0; i < nelem(irqlo); i++)
		intrunset(&irqlo[i]);
	for(i = 0; i < nelem(irqhi); i++)
		intrunset(&irqhi[i]);
	for(i = 0; i < nelem(irqbridge); i++)
		intrunset(&irqbridge[i]);

	/* disable all interrupts */
	INTRREG->lo.fiqmask = 0;
	INTRREG->hi.fiqmask = 0;
	INTRREG->lo.irqmask = 0;
	INTRREG->hi.irqmask = 0;
	INTRREG->lo.epmask = 0;
	INTRREG->hi.epmask = 0;
	CPUCSREG->irqmask = 0;

	/* clear interrupts */
	INTRREG->lo.irq = ~0;
	INTRREG->hi.irq = ~0;
	CPUCSREG->irq = ~0;

	intrenable(Irqlo, IRQ0sum, intrhi, nil, "hi");
	intrenable(Irqlo, IRQ0bridge, intrbridge, nil, "bridge");
}

static char *trapnames[PsrMask+1] = {
	[ PsrMfiq ] "Fiq interrupt",
	[ PsrMirq ] "Mirq interrupt",
	[ PsrMsvc ] "SVC/SWI Exception",
	[ PsrMabt ] "Prefetch Abort/Data Abort",
	[ PsrMabt+1 ] "Data Abort",
	[ PsrMund ] "Undefined instruction",
	[ PsrMsys ] "Sys trap"
};

static char *
trapname(int psr)
{
	char *s;

	s = trapnames[psr & PsrMask];
	if(s == nil)
		s = "Undefined trap";
	return s;
}

static void
sys_trap_error(int type)
{
	char errbuf[ERRMAX];
	sprint(errbuf, "sys: trap: %s\n", trapname(type));
	error(errbuf);
}
void
dflt(Ureg *ureg, ulong far)
{
	char buf[ERRMAX];

	dumpregs(ureg);
	sprint(buf, "trap: fault pc=%8.8lux addr=0x%lux", (ulong)ureg->pc, far);
	disfault(ureg, buf);
}

/*
 * based on ../pxa/trap.c:/^trap\(
 * xxx needs to be fleshed out (fiq?  handling prefetch/abort?)
 */
void
trap(Ureg *ureg)
{
	ulong far, fsr;
	int rem, t, itype;

	if(up != nil)
		rem = ((char*)ureg)-up->kstack;
	else
		rem = ((char*)ureg)-(char*)m->stack;
	if(ureg->type != PsrMfiq && rem < 256)
		panic("trap %d bytes remaining (%s), up=#%8.8lux ureg=#%8.8lux pc=#%8.8ux",
			rem, up?up->text:"", up, ureg, ureg->pc);

	itype = ureg->type;
	if(itype == PsrMabt+1)
		ureg->pc -= 8;
	else
		ureg->pc -= 4;
	ureg->sp = (ulong)(ureg+1);

	if(up){
		up->pc = ureg->pc;
		up->dbgreg = ureg;
	}
	switch(itype) {
	case PsrMirq:
		t = m->ticks;	/* CPU time per proc */
		up = nil;		/* no process at interrupt level */
		splflo();	/* allow fast interrupts */
		intrs(ureg, Irqlo);
		up = m->proc;
		preemption(m->ticks - t);
		break;

	case PsrMund:				/* Undefined instruction */
		if(*(ulong*)ureg->pc == BREAK && breakhandler) {
			int s;
			Proc *p;

			p = up;
			/* if(!waslo(ureg->psr) || ureg->pc >= (ulong)splhi && ureg->pc < (ulong)islo)
				p = 0; */
			s = breakhandler(ureg, p);
			if(s == BrkSched) {
				p->preempted = 0;
				sched();
			} else if(s == BrkNoSched) {
				p->preempted = 1;	/* stop it being preempted until next instruction */
				if(up)
					up->dbgreg = 0;
				return;
			}
			break;
		}
		if(up == nil)
			goto faultpanic;
		spllo();
		if(waserror()) {
			if(waslo(ureg->psr) && up->type == Interp)
				disfault(ureg, up->env->errstr);
			setpanic();
			dumpregs(ureg);
			panic("%s", up->env->errstr);
		}
		if(!fpiarm(ureg)) {
			dumpregs(ureg);
			sys_trap_error(ureg->type);
		}
		poperror();
		break;

	case PsrMsvc:				/* Jump through 0 or SWI */
		if(waslo(ureg->psr) && up && up->type == Interp) {
			spllo();
			dumpregs(ureg);
			sys_trap_error(ureg->type);
		}
		setpanic();
		dumpregs(ureg);
		panic("SVC/SWI exception");
		break;

	case PsrMabt:				/* Prefetch abort */
		if(catchdbg && catchdbg(ureg, 0))
			break;
		/* FALL THROUGH */
	case PsrMabt+1:			/* Data abort */
/* xxx we don't do mmu, remove this? */
if(0) {
		fsr = mmugetfsr();
		far = mmugetfar();
		if(fsr & (1<<9)) {
			mmuputfsr(fsr & ~(1<<9));
			if(catchdbg && catchdbg(ureg, fsr))
				break;
			print("Debug/");
		}
		if(waslo(ureg->psr) && up && up->type == Interp) {
			spllo();
			faultarm(ureg, far);
		}
		iprint("Data Abort: FSR %8.8luX FAR %8.8luX\n", fsr, far); xdelay(1);
}
		/* FALL THROUGH */

	default:				/* ??? */
faultpanic:
		setpanic();
		dumpregs(ureg);
		panic("exception %uX %s\n", ureg->type, trapname(ureg->type));
		break;
	}

	splhi();
	if(up)
		up->dbgreg = 0;		/* becomes invalid after return from trap */
}

void
setpanic(void)
{
	extern void screenon(int);
	extern int consoleprint;

	if (breakhandler != 0)	/* don't mess up debugger */
		return;

	maskallints();
	spllo();
	/* screenon(!consoleprint); */
	consoleprint = 1;
	serwrite = serialputs;
}

void
dumpstack(void)
{
}

void
dumpregs(Ureg*)
{
}
