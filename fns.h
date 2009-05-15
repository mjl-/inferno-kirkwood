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
void	archreset(void);
void	archreboot(void);
void	clockinit(void);
void	clockcheck(void);
void	clockpoll(void);
void	dumpregs(Ureg* ureg);
int	fpiarm(Ureg*);
void	fpinit(void);
ulong	getcallerpc(void*);
void	idlehands(void);
void	intrclear(int, int);
void	intrmask(int, int);
void	intrunmask(int, int);
void	intrdisable(int, int, void (*)(Ureg*, void*), void*, char*);
void	intrenable(int, int, void (*)(Ureg*, void*), void*, char*);
void	links(void);
void	screeninit(void);
void	trapinit(void);

int	splfhi(void);
int	splflo(void);
ulong	cpsrr(void);
ulong	spsrr(void);
void	vectors(void);
void	vtable(void);

void	(*screenputs)(char*, int);

int	rprint(char *, ...);
