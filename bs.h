typedef struct Store Store;

struct Store
{
	int	num;
	char	name[4+1];	/* "bs%02d", num */
	void	*ctlr;

	/* set by init, used by devbs.c */
	Part	*parts;		/* first is always "data" for whole disk */
	int	nparts;
	vlong	size;

	void	(*init)(Store *d);		/* init controller */
	void	(*diskinit)(Store *d);		/* find disk and set size */
	int	(*rctl)(Store *d, char *s);
	int	(*wctl)(Store *d, char *s);
	long	(*io)(void *d, int iswrite, void *buf, long n, vlong off);
};

extern void blockdiskadd(Store *);
