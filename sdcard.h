enum {
	Respmax		= (136+8-1)/8,
};

typedef struct Card Card;
typedef struct Cid Cid;
typedef struct Csd Csd;

struct Cid
{
	uint	mid;		/* manufacturer id */
	char	oid[2+1];	/* oem id */
	char	prodname[5+1];	/* product name */
	uint	rev;		/* product revision */
	ulong	serial;
	int	year;
	int	mon;
};

/* xxx make shorter but still readable names */
struct Csd
{
	int	version;
	int	taac, nsac;
	int	xferspeed;
	int	cmdclasses;
	int	readblocklength, readblockpartial;
	int	writeblockmisalign, readblockmisalign;
	int	dsr;
	int	size;
	union {
		struct {
			int	vddrmin, vddrmax, vddwmin, vddwmax;
			int	sizemult;
		} v0;
	};
	int	eraseblockenable, erasesectorsize;
	int	wpgroupsize, wpgroupenable;
	int	writespeedfactor;
	int	writeblocklength, writeblockpartial;
	int	fileformatgroup;
	int	copy;
	int	permwriteprotect, tmpwriteprotect;
	int	fileformat;
};

struct Card {
	int	valid;
	Cid	cid;
	Csd	csd;
	ulong	bs;
	uvlong	size;
	int	mmc;
	int	sd2;
	int	sdhc;
	uint	rca;
	uchar	resp[Respmax];
	int	lastcmd;
	int	lastisapp;
	int	lasterr;
};

ulong	bits(uchar *p, int msb, int lsb);

int	parsecid(Cid *c, uchar *r);
char*	cidstr(char *p, char *e, Cid *c);
int	parsecsd(Csd *c, uchar *r);
char*	csdstr(char *p, char *e, Csd *c);
char*	cardtype(Card *c);
char*	cardstr(Card *c, char *buf, int n);
