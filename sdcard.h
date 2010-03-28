typedef struct Card Card;
typedef struct Cid Cid;
typedef struct Csd Csd;
typedef struct Scr Scr;

struct Cid
{
	uint	mid;		/* manufacturer id */
	char	oid[2+1];	/* oem id */
	char	prodname[5+1];	/* product name */
	ulong	rev;		/* product revision */
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

enum {
	/* Scr.spec */
	Spec101,
	Spec110,
	Spec200,

	/* Scr.buswidth */
	Bus1bit		= 1<<0,
	Bus4bit		= 1<<2,
};
struct Scr
{
	uchar	vers;			/* version of scr struct */
	uchar	spec;			/* sd physical layer version supported by card */
	uchar	dataerased;		/* data after erase, 0 or 1 */
	uchar	sec;			/* sd security supported */
	uchar	buswidth;		/* data bus width */
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
	uvlong	resp[3];
	ulong	status;			/* r1 card status, for SDBadstatus;  SDIOREG->est for SDError */
};

int	parsecid(Cid *c, uvlong *r);
char*	cidstr(char *p, char *e, Cid *c);
int	parsecsd(Csd *c, uvlong *r);
char*	csdstr(char *p, char *e, Csd *c);
char*	cardtype(Card *c);
char*	cardstr(Card *c, char *buf, int n);
int	parsescr(Scr *s, uvlong *r);
