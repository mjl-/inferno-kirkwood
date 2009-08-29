#include "../port/portfns.h"

void	setpanic(void);
void	setr13(int, void*);
#define waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
#define procsave(p)
#define procrestore(p)
#define coherence()

#define KADDR(p)	((void *)p)
#define PADDR(p)	((ulong)p)

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
ulong	getcallerpc(void*);
ulong	getcpuid(void);
char*	getconf(char *);
void	gotopc(ulong);
void	idlehands(void);
void	idle(void);
void	intrclear(int, int);
void	intrmask(int, int);
void	intrunmask(int, int);
int	intrdisable(int, int, void (*)(Ureg*, void*), void*, char*);
void	intrenable(int, int, void (*)(Ureg*, void*), void*, char*);
void	kbdinit(void);
void	links(void);
void 	microdelay(int us);
ulong	mmuinit(void);
int	pcmspecial(char *, ISAConf *);
void	screeninit(void);
void	trapinit(void);
void	uartconsole(void);

int	splfhi(void);
int	splflo(void);
ulong	cpsrr(void);
ulong	spsrr(void);
void	vectors(void);
void	vtable(void);

void	serialputs(char *, int);
void	(*screenputs)(char*, int);

/* xor.c */
void	meminitdma(uchar *buf, long n, uvlong v);
ulong	crc32cdma(uchar *buf, ulong n);
void	memdma(uchar *dst, uchar *src, ulong n);
void	xordma(uchar *dst, uchar **src, int nsrc, ulong n);

ulong	tcmstat(void);
ulong	rcpctl(void);
ulong	wcpctl(ulong);

void	dcflush(void*, ulong);
void	dcflushall(void);
void	icflush(void*, ulong);
void	icflushall(void);
int	segflush(void*, ulong);
