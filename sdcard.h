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

struct Csd
{
	uchar	vers;		/* version */
	uchar	taac;		/* data read access time */
	uchar	nsac;		/* idem */
	uchar	speed;		/* transfer speed */
	ushort	ccc;		/* card command classes */
	uchar	rbl;		/* read block length, log 2 */
	uchar	rbpart;		/* partial block reads allowed */
	uchar	wbmalign;	/* writes on misaligned blocks allowed */
	uchar	rbmalign;	/* idem for reads */
	uchar	dsr;		/* whether dsr is implemented */
	uint	size;		/* for calculating size */
	union {
		struct {
			uchar	vddrmin;
			uchar	vddrmax;
			uchar	vddwmin;
			uchar	vddwmax;
			uchar	sizemult;	/* available bytes:  (size+1)*2**(sizemult+2) */
		} v0;
	};
	uchar	eraseblk;	/* if areas smaller than sector can be erased */
	uchar	erasesecsz;	/* (erasesecsz+1) is number of wbl blocks that are erased, for eraseblk */
	uchar	wpgrpsize;	/* write protected group size */
	uchar	wpgrp;		/* idem, enabled */
	uchar	speedfactor;	/* read/write speed factor */
	uchar	wbl;		/* write block length, log 2 */
	uchar	wbpart;		/* partial block writes allowed */
	uchar	ffgrp;		/* file format group */
	uchar	copy;		/* is this a copy? */
	uchar	permwp;		/* permanently write protected */
	uchar	tmpwp;		/* temporarily write protected */
	uchar	ff;		/* file format */
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
