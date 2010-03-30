typedef struct Store Store;

struct Store
{
	RWlock;			/* devbs rlocks for: rctl,wctl,io,raw.  wlocks for: init,diskinit,modifying values */
	void	*ctlr;

	int	vers;			/* version, increased for each devinit. */
	int	ready;			/* whether device initialised and ready for use */
	int	num;
	char	name[4+1];		/* "bs%02d", num */
	int	alignmask;		/* alignment, bits must be zero */
	char	*descr;			/* description, from device */
	char	devtype[KNAMELEN];	/* type of device, returned when reading bsXX/ctl */

	/* set by init, used by devbs.c */
	Part	*parts;		/* first is always "data" for whole disk */
	int	nparts;
	vlong	size;

	void	(*init)(Store *d);		/* init controller */
	void	(*devinit)(Store *d);		/* find disk and set size */
	long	(*rctl)(Store *d, void *s, long n, vlong off);
	long	(*wctl)(Store *d, void *s, long n);
	long	(*io)(void *d, int iswrite, void *buf, long n, vlong off);
	long	(*raw)(Store *d, void *s, long n, vlong off, void **r);
};

void blockstoreadd(Store *);
