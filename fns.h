#include "../port/portfns.h"

void	setpanic(void);
void	setr13(int, void*);
#define waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
#define procsave(p)
#define procrestore(p)
#define coherence()

#define KADDR(p)	((void *)p)
#define PADDR(p)	((ulong)p)

void	archcpufreq(int slow);
void	archconfinit(void);
void	archconsole(void);
void	archreset(void);
void	archreboot(void);
void	clockinit(void);
void	clockcheck(void);
void	clockpoll(void);
void	delay(int ms);
void	dumpregs(Ureg*);
int	fpiarm(Ureg*);
void	fpinit(void);
char*	getconf(char *);
void	idlehands(void);
void	intrclear(int, int);
void	intrmask(int, int);
void	intrunmask(int, int);
int	intrdisable(int, int, void (*)(Ureg*, void*), void*, char*);
void	intrenable(int, int, void (*)(Ureg*, void*), void*, char*);
void	kbdinit(void);
void	links(void);
void 	microdelay(int us);
int	pcmspecial(char *, ISAConf *);
void	screeninit(void);
void	trapinit(void);
void	uartconsole(void);

void	serialputs(char *, int);
void	(*screenputs)(char*, int);

/* xor.c */
void	meminitdma(uchar *buf, long n, uvlong v);
ulong	crc32cdma(uchar *buf, ulong n);
void	memdma(uchar *dst, uchar *src, ulong n);
void	xordma(uchar *dst, uchar **src, int nsrc, ulong n);

/* l.s */
ulong	getcallerpc(void*);
void	gotopc(ulong);
void	idle(void);

int	splfhi(void);
int	splflo(void);
ulong	cpsrr(void);
ulong	spsrr(void);
void	vectors(void);
void	vtable(void);

ulong	cpuidget(void);
ulong	cacheget(void);
ulong	tcmget(void);
ulong	cpctlget(void);
ulong	cpctlput(ulong);
ulong	ttbget(void);
void	ttbput(ulong);
ulong	dacget(void);
void	dacput(ulong);
ulong	dclockdownget(void);
void	dclockdownput(ulong);
ulong	iclockdownget(void);
void	iclockdownput(ulong);
ulong	tlblockdownget(ulong);
ulong	tlblockdownput(ulong);
ulong	fcsepidget(void);
void	fcsepidput(ulong);
ulong	contextidget(void);
void	tlbclear(void);

void	icinv(void*, ulong);
void	icinvall(void);

void	dcwb(void*, ulong);
void	dcwball(void);
void	dcwbinv(void*, ulong);
void	dcwbinvall(void);
void	dcinv(void*, ulong);
void	dcinvall(void);

int	segflush(void*, ulong);
