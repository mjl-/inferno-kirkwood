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
void	dumpregs(Ureg* ureg);
int	fpiarm(Ureg*);
void	fpinit(void);
ulong	getcallerpc(void*);
ulong	getcpuid(void);
void	idlehands(void);
void	idle(void);
void	intrclear(int, int);
void	intrmask(int, int);
void	intrunmask(int, int);
void	intrdisable(int, int, void (*)(Ureg*, void*), void*, char*);
void	intrenable(int, int, void (*)(Ureg*, void*), void*, char*);
void	kbdinit(void);
void	links(void);
void	mmuinit(void);
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
