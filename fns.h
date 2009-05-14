#include "../port/portfns.h"

void	setpanic(void);
void	setr13(int, void*);
#define waserror()	(up->nerrlab++, setlabel(&up->errlab[up->nerrlab-1]))
#define procsave(p)
#define procrestore(p)
#define coherence()

#define KADDR(p)	((void *)p)
#define PADDR(p)	((ulong)p)

void	clockinit(void);
void	clockcheck(void);
void	clockpoll(void);
void	links(void);
ulong	getcallerpc(void*);
void	idlehands(void);
void	archconfinit(void);
void	archreset(void);
void	archreboot(void);
void	trapinit(void);
void	vectors(void);
void	vtable(void);
void	screeninit(void);
int	splfhi(void);
int	splflo(void);
ulong	cpsrr(void);
ulong	spsrr(void);


void	(*screenputs)(char*, int);
int	rprint(char *, ...);
