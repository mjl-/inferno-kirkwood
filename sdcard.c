#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"io.h"
#include	"sdcard.h"


static int
min(int a, int b)
{
	if(a < b)
		return a;
	return b;
}

/* return bits h(igh) to l(ow) (inclusive) from big endian r[3] */
static ulong
bits(uvlong r[3], int h, int l)
{
	uvlong v;
	int o;
	int n;

	v = 0;
	o = 0;
	r += 2;
	while(h > l && l >= 0) {
		if(l < 64) {
			n = min(h, 63)-l+1;
			v |= ((*r>>l) & ((1ULL<<n)-1)) << o;
			l += n;
			o += n;
		}
		h -= 64;
		l -= 64;
		r--;
	}
	return v;
}

int
parsecid(Cid *c, uvlong *r)
{
	int i;

	c->mon		= bits(r, 11, 8);
	c->year		= 2000 + bits(r, 19, 16)*10 + bits(r, 15, 12);
	c->serial	= bits(r, 55, 24);
	c->rev		= bits(r, 63, 56);

	for(i = 0; i < 5; i++)
		c->prodname[i] = bits(r, 104-i*8-1, 104-i*8-8);
	c->prodname[5] = '\0';

	c->oid[0] = bits(r, 119, 112);
	c->oid[1] = bits(r, 111, 104);
	c->oid[2] = '\0';

	c->mid = bits(r, 127, 120);
	return 0;
}

char*
cidstr(char *p, char *e, Cid *c)
{
	return seprint(p, e,
		"product %s, rev %#lux, serial %#lux, made %04d-%02d, oem %s, manufacturer %#ux\n",
		c->prodname,
		c->rev,
		c->serial,
		c->year, c->mon,
		c->oid,
		c->mid);
}

int
parsecsd(Csd *c, uvlong *r)
{
	c->vers = bits(r, 127, 126);
	if(c->vers != 0 && c->vers != 1)
		return -1;

	c->taac			= bits(r, 119, 112);
	c->nsac			= bits(r, 111, 104);
	c->speed		= bits(r, 103, 96);
	c->ccc			= bits(r, 95, 84);
	c->rbl			= bits(r, 83, 80);
	c->rbpart		= bits(r, 79, 79);
	c->wbmalign		= bits(r, 78, 78);
	c->rbmalign		= bits(r, 77, 77);
	c->dsr			= bits(r, 76, 76);
	if(c->vers == 0) {
		c->size		= bits(r, 75, 62);
		c->v0.vddrmin	= bits(r, 61, 59);
		c->v0.vddrmax	= bits(r, 58, 56);
		c->v0.vddwmin	= bits(r, 55, 53);
		c->v0.vddwmax	= bits(r, 52, 50);
		c->v0.sizemult	= bits(r, 49, 47);
	} else {
		c->size		= bits(r, 69, 48);
	}
	c->eraseblk		= bits(r, 46, 46);
	c->erasesecsz		= bits(r, 45, 39);
	c->wpgrpsize		= bits(r, 38, 32);
	c->wpgrp		= bits(r, 31, 31);
	c->speedfactor		= bits(r, 28, 26);
	c->wbl			= bits(r, 25, 22);
	c->wbpart		= bits(r, 21, 21);
	c->ffgrp		= bits(r, 15, 15);
	c->copy			= bits(r, 14, 14);
	c->permwp		= bits(r, 13, 13);
	c->tmpwp		= bits(r, 12, 12);
	c->ff			= bits(r, 11, 10);

	return 0;
}

char*
csdstr(char *p, char *e, Csd *c)
{
	char versbuf[128];

	versbuf[0] = '\0';
	if(c->vers == 0)
		snprint(versbuf, sizeof versbuf,
			"vddrmin %x\n"
			"vddrmax %x\n"
			"vddwmin %x\n"
			"vddwmax %x\n"
			"sizemult %x\n",
			c->v0.vddrmin,
			c->v0.vddrmax,
			c->v0.vddwmin,
			c->v0.vddwmax,
			c->v0.sizemult);
	return seprint(p, e,
		"version %ux\n"
		"taac %ux\n"
		"nsac %ux\n"
		"transferspeed %ux\n"
		"card command classes %ux\n"
		"readblocklength %ux\n"
		"readblockpartial %ux\n"
		"writeblockmisalign %ux\n"
		"readblockmisalign %ux\n"
		"dsr %ux\n"
		"devsize %ux\n"
		"%s"
		"eraseblockenable %ux\n"
		"erasesectorsize %ux\n"
		"wpgroupsize %ux\n"
		"wpgroupenable %ux\n"
		"writespeedfactor %ux\n"
		"writeblocklength %ux\n"
		"writeblockpartial %ux\n"
		"fileformatgroup %ux\n"
		"copy %ux\n"
		"permwriteprotect %ux\n"
		"tmpwriteprotect %ux\n"
		"fileformat %ux\n",
		(uint)c->vers,
		(uint)c->taac,
		(uint)c->nsac,
		(uint)c->speed,
		(uint)c->ccc,
		(uint)c->rbl,
		(uint)c->rbpart,
		(uint)c->wbmalign,
		(uint)c->rbmalign,
		(uint)c->dsr,
		(uint)c->size,
		versbuf,
		(uint)c->eraseblk,
		(uint)c->erasesecsz,
		(uint)c->wpgrpsize,
		(uint)c->wpgrp,
		(uint)c->speedfactor,
		(uint)c->wbl,
		(uint)c->wbpart,
		(uint)c->ffgrp,
		(uint)c->copy,
		(uint)c->permwp,
		(uint)c->tmpwp,
		(uint)c->ff);
}

char*
cardtype(Card *c)
{
	if(c->mmc)
		return "mmc";
	if(c->sdhc)
		return "sdhc";
	return "sd";
}

char*
cardstr(Card *c, char *buf, int n)
{
	snprint(buf, n,
		"card %s\n"
		"type %s\n"
		"size %lld bytes\n"
		"blocksize %lud\n"
		"manufactured %d-%02d\n"
		"rev %#lux\n"
		"serial %#lux\n",
		c->cid.prodname,
		cardtype(c),
		c->size,
		c->bs,
		c->cid.year, c->cid.mon,
		c->cid.rev,
		c->cid.serial);
	return buf;
}

int
parsescr(Scr *s, uvlong *r)
{
	s->vers		= bits(r, 63, 60);
	s->spec		= bits(r, 59, 56);
	s->dataerased	= bits(r, 55, 55);
	s->sec		= bits(r, 54, 52);
	s->buswidth	= bits(r, 51, 48);

	return 0;
}
