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
extern int panicreset;

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
 * parse u-boot parameters.
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
	uvlong nb;

	nb = conf.npage*BY2PG;
	poolsize(mainmem, (int)((nb*main_pool_pcnt)/100), 0);
	poolsize(heapmem, (int)((nb*heap_pool_pcnt)/100), 0);
	poolsize(imagmem, (int)((nb*image_pool_pcnt)/100), 1);
}

static void
l2print(void)
{
	ulong v;

	v = CPUCSREG->l2cfg;
	print("l2: %s, ecc %s, mode %s\n", (v&L2enable) ? "on" : "off", (v&L2ecc) ? "on" : "off", (v & L2wtmode) ? "writethrough" : "writeback");

if(0) {
	int i;
	L2winReg *l = L2WINREG;

	print("l2cfg: %#lux\n", v);
	print("l2 non cacheable regions:\n");
	for(i = 0; i < nelem(l->win); i++)
		print("addr %#lux, size %#lux (%s)\n", l->win[i].addr, l->win[i].size, (l->win[i].addr&1) ? "on" : "off");
}
}

static char*
onoff(ulong v)
{
	if(v)
		return "on";
	return "off";
}

static void
cacheprint0(char *s, int on, ulong v)
{
	ulong len, m, assoc, sz;

	len = (v>>0) & MASK(2);
	m = (v>>2) & MASK(1);
	assoc = (v>>3) & MASK(3);
	sz = (v>>6) & MASK(4);
	if(m == 0)
		print("%s %s, %dkb, %s associative, %s lines\n",
			s,
			onoff(on),
			1<<(sz-1),
			(assoc == 0x02) ? "4-way" : "unknown",
			(len == 0x02) ? "32 byte" : "unknown sized");
	else
		print("%s absent\n", s);
}


enum {
	/* domain access control register.  16x 2-bits. */
	Client		= 0x1,	/* obey access permission bits in descriptors */
	Manager 	= 0x3,	/* do not obey ... */

	/* coprocessor control register */
	Roundrobin	= 1<<14,
	Icacheena	= 1<<12,
	Romprot		= 1<<9,
	Systemprot	= 1<<8,
	Bigendian	= 1<<7,
	Dcacheena	= 1<<2,
	Alignfault	= 1<<1,
	MMUena		= 1<<0,

	/* section descriptor */
	AP		= 0x3<<10,
	Cacheable	= 1<<3,
	Bufferable	= 1<<2,
	Sectiondescr	= (1<<4)|(2<<0),
};
static void
cacheprint(void)
{
	ulong v;
	ulong isz, dsz;

	v = cacheget();
	isz = (v>>0) & MASK(12);
	dsz = (v>>12) & MASK(12);
	v = cpctlget();
	cacheprint0("icache", v&Icacheena, isz);
	cacheprint0("dcache", v&Dcacheena, dsz);

	if(0)print("mmu %s, %s endian, align faults %s, protection: rom %s, system %s\n",
		onoff(v&MMUena),
		(v&Bigendian) ? "big" : "little",
		onoff(Alignfault), 
		onoff(v&Romprot),
		onoff(v&Systemprot));
}

/* mmu is required to use dcache. */
static void
mmuinit(void)
{
	ulong *p;
	ulong i;

	p = xspanalloc(16*1024, 16*1024, 0);
	if(p == nil)
		panic("no memory for mmu");

	/*
	 * invalidate all descriptors, then map with va == vma == pa:
	 * - first 512mb on sdram, cacheable & bufferable (cb).
	 * - register file & nand interface, non-cacheable & non-bufferable (ncnb).
	 * note: we may want to add more ncnb sections, e.g. for pci express
	 */
	memset(p, 0, 16*1024);
	for(i = 0; i < 512; i++)
		p[i] = (i<<20)|AP|Cacheable|Bufferable|Sectiondescr;
	p[Regbase>>20] = (Regbase&~MASK(20))|AP|Sectiondescr;
	p[AddrPhyNand>>20] = (AddrPhyNand&~MASK(20))|AP|Sectiondescr;

	/* enable mmu & l1 caches */
	ttbput((ulong)p);	/* translation table base address */
	dacput(Manager<<0);	/* we only use dom 0, all accesses allowed */
	fcsepidput(0);		/* pid used in mva (modified va).  always 0 for us. */
	dclockdownput(0xfff<<4);	/* bits 3..0 set the locked Ways */
	dcinvall();
	tlbclear();
	/* xxx should set the 8 locked down tlb entries.  for performance, but also because they now may contain bad entries. */
	cpctlput(cpctlget()|MMUena|Icacheena|Dcacheena|Alignfault);
}

/*
xxx something is wrong with flushing the dcache.
perhaps the special test & clean cp mrc instructions _do_ modify r15?
*/
void dcwb0(void *, ulong);
void dcwbinv0(void *, ulong);

void
dcwb(void *p, ulong n)
{
	if(0 && n > CACHESIZE/2)
		dcwball();
	else
		dcwb0(p, n);
}

void
dcwbinv(void *p, ulong n)
{
	if(0 && n > CACHESIZE/2)
		dcwbinvall();
	else
		dcwbinv0(p, n);
}

void
main(void)
{
	CPUCSREG->l2cfg &= ~L2enable;

	/* invalidate & enable l1 icache */
	iclockdownput(0xfff<<4);	/* bits 3..0 set the locked Ways */
	icinvall();
	cpctlput(cpctlget()|Icacheena);

	memset(edata, 0, end-edata);	/* clear bss */
	memset(m, 0, sizeof(Mach));	/* clear mach */
	conf.nmach = 1;

	archreset();
	quotefmtinstall();
	confinit();
	xinit();
	iprint("mmuinit\n");
	mmuinit();
	poolinit();
	poolsizeinit();
	options();
	trapinit();
	clockinit();
	printinit();
	procinit();
	archconsole();
	iprint("links\n");
	links();
	iprint("chandevreset\n");
	chandevreset();

	eve = strdup("inferno");

	iprint("kbdinit\n");
	kbdinit();

	iprint("prints\n");
	print("%ld MHz, id %08lux\n", (m->cpuhz+500000)/1000000, cpuidget());
	print("\nInferno %s\n", VERSION);
	print("Vita Nuova\n");
	print("conf %s (%lud) jit %d\n\n", conffile, kerndate, cflag);
	print("kirkwood %s\n\n", conf.devidstr);

	l2print();
	cacheprint();

	iprint("userinit\n");
	userinit();
	iprint("schedinit\n");
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
	conf.npage0 = (conf.topofmem - base)/BY2PG;

	conf.base1 = 0;
	conf.npage1 = 0;

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

	chandevshutdown();

	if(inpanic && !panicreset){
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

int
segflush(void *a, ulong n)
{
	dcwb(a, n);
	icinv(a, n);
	//l2cache(a, n);
	return 0;
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
	/* sdram self-refresh mode.  starts after 256 cycles, until next memory access. */
	SDRAMCREG->oper = 0x7;

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
