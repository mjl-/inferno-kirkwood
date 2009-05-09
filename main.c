#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "version.h"

Mach *m = (Mach*)MACHADDR;
Proc *up = 0;
Vectorpage *page0 = (Vectorpage*)KZERO;
Conf conf;

extern ulong kerndate;
extern int cflag;
extern int main_pool_pcnt;
extern int heap_pool_pcnt;
extern int image_pool_pcnt;
ulong cpuidlecount;

static void
poolsizeinit(void)
{
	ulong nb;

	nb = conf.npage*BY2PG;
	poolsize(mainmem, (nb*main_pool_pcnt)/100, 0);
	poolsize(heapmem, (nb*heap_pool_pcnt)/100, 0);
	poolsize(imagmem, (nb*image_pool_pcnt)/100, 1);
}

void
delay(void)
{
	int i;

	i = 0;
	while(i < (1<<17))
		i++;
}

void
putc(char c)
{
	UartReg *r = UART0REG;

	while((r->lsr&LSRthre) == 0)
		delay();
	r->thr = c;
}

void
puts(char *s)
{
	while(*s != 0)
		putc(*s++);
}

uchar
hex(ulong v)
{
	v &= 0xf;
	if(v <= 9)
		return '0'+v;
	return 'a'-10+v;
}

void
putul(ulong v)
{
	char buf[] = "0x33221100 ";
	uchar *p;

	p = (uchar*)buf+2;
	*p++ = hex(v>>28);
	*p++ = hex(v>>24);
	*p++ = hex(v>>20);
	*p++ = hex(v>>16);
	*p++ = hex(v>>12);
	*p++ = hex(v>>8);
	*p++ = hex(v>>4);
	*p++ = hex(v>>0);

	puts(buf);
}

void
main(void)
{
	IntrReg *intr = INTRREG;
	CpucsReg *cpucs = CPUCSREG;
	TimerReg *tmr = TIMERREG;

	memset(edata, 0, end-edata);	/* clear bss */
	memset(m, 0, sizeof(Mach));	/* clear mach */
	conf.nmach = 1;

	puts("before trapinit\r\n");
	trapinit();
	puts("before clockinit\r\n");
	clockinit();

	puts("waiting\r\n");
	spllo();

	/* wait */
	for(;;)
		;

	/* xxx debug prints */
	for(;;) {
		putul(intr->lo.irq);
		//putul(intr->lo.irqmask);
		putul(cpucs->irq);
		//putul(cpucs->irqmask);
		putul(tmr->timer0);
		putul(tmr->ctl);
		putul(cpsrr());
		putul(spsrr());
		puts("\r\n");
		//cpucs->irq = 0;
		//intr->lo.irq = 0;
	}

	quotefmtinstall();
	archreset();
	confinit();
	xinit();
	/* mmuinit(); */
	poolinit();
	poolsizeinit();
	trapinit();
	clockinit();
	printinit();
	/* screeninit() */
	procinit();
	links();
	chandevreset();

	eve = strdup("inferno");

	/* archconsole(); */
	/* kbdinit(); */

	print("\nInferno %s\n", VERSION);
	print("Vita Nuova\n");
	print("conf %s (%lud) jit %d\n\n", conffile, kerndate, cflag);

	userinit();
	schedinit();
}

void
reboot(void)
{
	exit(0);
}

void
halt(void)
{
	spllo();
	print("cpu halted\n");
	for(;;){
	/* nothing to do */
	}
}

void
confinit(void)
{
	ulong base;

	archconfinit();

	base = PGROUND((ulong)end);
	conf.base0 = base;

	conf.base1 = 0;
	conf.npage1 = 0;

	conf.npage0 = (conf.topofmem - base)/BY2PG;

	conf.npage = conf.npage0 + conf.npage1;
	conf.ialloc = (((conf.npage*(main_pool_pcnt))/100)/2)*BY2PG;

	conf.nproc = 100 + ((conf.npage*BY2PG)/MB)*5;
	conf.nmach = 1;
}

void
init0(void)
{
	Osenv *o;
	char buf[2*KNAMELEN];

	up->nerrlab = 0;

	spllo();

	if(waserror())
		panic("init0 %r");
	/*
	 * These are o.k. because rootinit is null.
	 * Then early kproc's will have a root and dot.
	 */
	o = up->env;
	o->pgrp->slash = namec("#/", Atodir, 0, 0);
	cnameclose(o->pgrp->slash->name);
	o->pgrp->slash->name = newcname("/");
	o->pgrp->dot = cclone(o->pgrp->slash);

	chandevinit();

	if(!waserror()){
		ksetenv("cputype", "arm", 0);
		snprint(buf, sizeof(buf), "arm %s", conffile);
		ksetenv("terminal", buf, 0);
		poperror();
	}

	poperror();

	disinit("/osinit.dis");
}

void
userinit(void)
{
	Proc *p;
	Osenv *o;

	p = newproc();
	o = p->env;

	o->fgrp = newfgrp(nil);
	o->pgrp = newpgrp();
	o->egrp = newegrp();
	kstrdup(&o->user, eve);

	strcpy(p->text, "interp");

	p->fpstate = FPINIT;

	/*
	 * Kernel Stack
	 *
	 * N.B. The -12 for the stack pointer is important.
	 *	4 bytes for gotolabel's return PC
	 */
	p->sched.pc = (ulong)init0;
	p->sched.sp = (ulong)p->kstack+KSTACK-8;

	ready(p);
}

void
exit(int inpanic)
{
	up = 0;

	/* Shutdown running devices */
	chandevshutdown();

	if(inpanic && 0){
		print("Hit the reset button\n");
		for(;;)
			clockpoll();
	}
	archreboot();
}

static void
linkproc(void)
{
	spllo();
	if (waserror())
		print("error() underflow: %r\n");
	else
		(*up->kpfun)(up->arg);
	pexit("end proc", 1);
}

void
segflush(void *, ulong)
{
	/* xxx */
}

void
kprocchild(Proc *p, void (*func)(void*), void *arg)
{
	p->sched.pc = (ulong)linkproc;
	p->sched.sp = (ulong)p->kstack+KSTACK-8;

	p->kpfun = func;
	p->arg = arg;
}

void
idlehands(void)
{
	/* xxx silence processor until interrupt */
}

void
fpinit(void)
{
}

void
FPsave(void*)
{
}

void
FPrestore(void*)
{
}
