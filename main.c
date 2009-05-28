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

enum {
	MAXCONF	= 32,
};
char *confname[MAXCONF];
char *confval[MAXCONF];
int nconf;


void
addconf(char *name, char *val)
{
	if(nconf >= MAXCONF)
		return;
	confname[nconf] = name;
	confval[nconf] = val;
	nconf++;
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];
	return 0;
}

/*
 * parse linux boot parameters.
 * typically starting at 0x100, consisting of size,tag,data triplets.
 * size & tag are 4-byte words.  size is in 4-byte words,
 * including the two words for size,tag header.
 * we're looking for the nul-terminated data of the "cmdline" tag.
 * the first tag is "core", the last is "none".
 */
enum {
	Atagcore	= 0x54410001,
	Atagcmdline	= 0x54410009,
	Atagnone	= 0x00000000,
	Atagmax		= 4*1024,	/* parameters are supposed to be in first 16kb memory */
};
static void
options(void)
{
	ulong *p = (ulong*)0x100;
	ulong *e;
	ulong size;
	ulong tag;
	char *k, *v, *next;
	int end;

	e = p+Atagmax;
	for(;;) {
		size = *p++;
		tag = *p++;
		if(tag == Atagcore) {
			p += size-2;
			break;
		}
		if(p >= e)
			return; /* no options, bad luck */
	}

	/* just past atagcore */
	while(p < e) {
		size = *p++;
		tag = *p++;
		if(tag == Atagnone)
			return;
		if(tag == Atagcmdline) {
			k = (char*)p;
			while(*k != 0) {
				v = strchr(k, '=');
				*v = 0;
				next = strchr(++v, ' ');
				end = *next == 0;
				*next = 0;
				addconf(strdup(k), strdup(v));
				if(!end)
					k = next+1;
			}
			return;
		}
		p += size-2;
	}
}

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
main(void)
{
	memset(edata, 0, end-edata);	/* clear bss */
	memset(m, 0, sizeof(Mach));	/* clear mach */
	conf.nmach = 1;

	mmuinit();
	quotefmtinstall();
	archreset();
	confinit();
	xinit();
	poolinit();
	poolsizeinit();
	options();
	trapinit();
	clockinit();
	printinit();
	procinit();
	archconsole();
	links();
	chandevreset();

	eve = strdup("inferno");

	kbdinit();

	print("%ld MHz id %8.8lux\n", (m->cpuhz+500000)/1000000, getcpuid());
	print("\nInferno %s\n", VERSION);
	print("Vita Nuova\n");
	print("conf %s (%lud) jit %d\n\n", conffile, kerndate, cflag);
	print("kirkwood %s\n\n", conf.devidstr);

	print("scratch memory: %#8.8lux\n", xspanalloc(8*1024, 0x1000, 0));

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
	idle();
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
