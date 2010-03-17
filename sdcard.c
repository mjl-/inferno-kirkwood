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

ulong
bits(uchar *p, int msb, int lsb)
{
	ulong v;
	int nbits, o, n;

	nbits = msb-lsb+1;
	n = lsb/8;
	p += n;
	lsb -= n*8;

	n = min(nbits, 8-lsb);
	nbits -= n;
	v = (*p++>>lsb) & MASK(n);
	o = n;

	while(nbits > 0) {
		n = min(nbits, 8);
		nbits -= n;
		v |= ((ulong)*p++ & MASK(n)) << o;
		o += n;
	}
	return v;
}

int
parsecid(Cid *c, uchar *r)
{
	uchar *p;

	c->mon		= bits(r, 11, 8);
	c->year		= 2000 + bits(r, 19, 16)*10 + bits(r, 15, 12);
	c->serial	= bits(r, 55, 24);
	c->rev		= bits(r, 63, 56);

	p = r+64/8;
	c->prodname[0] = p[4];
	c->prodname[1] = p[3];
	c->prodname[2] = p[2];
	c->prodname[3] = p[1];
	c->prodname[4] = p[0];
	c->prodname[5] = '\0';

	p = r+104/8;
	c->oid[0] = p[1];
	c->oid[1] = p[0];
	c->oid[2] = '\0';

	c->mid		= bits(r, 127, 120);
	return 0;
}

char*
cidstr(char *p, char *e, Cid *c)
{
	return seprint(p, e,
		"product %s, rev %#ux, serial %#lux, made %04d-%02d, oem %s, manufacturer %#ux\n",
		c->prodname,
		c->rev,
		c->serial,
		c->year, c->mon,
		c->oid,
		c->mid);
}

int
parsecsd(Csd *c, uchar *r)
{
	c->version = bits(r, 127, 126);
	if(c->version != 0 && c->version != 1)
		return -1;

	c->taac			= bits(r, 119, 112);
	c->nsac			= bits(r, 111, 104);
	c->xferspeed		= bits(r, 103, 96);
	c->cmdclasses		= bits(r, 95, 84);
	c->readblocklength	= bits(r, 83, 80);
	c->readblockpartial	= bits(r, 79, 79);
	c->writeblockmisalign	= bits(r, 78, 78);
	c->readblockmisalign	= bits(r, 77, 77);
	c->dsr			= bits(r, 76, 76);
	if(c->version == 0) {
		c->size		= bits(r, 75, 62);
		c->v0.vddrmin	= bits(r, 61, 59);
		c->v0.vddrmax	= bits(r, 58, 56);
		c->v0.vddwmin	= bits(r, 55, 53);
		c->v0.vddwmax	= bits(r, 52, 50);
		c->v0.sizemult	= bits(r, 49, 47);
	} else {
		c->size		= bits(r, 69, 48);
	}
	c->eraseblockenable	= bits(r, 46, 46);
	c->erasesectorsize	= bits(r, 45, 39);
	c->wpgroupsize		= bits(r, 38, 32);
	c->wpgroupenable	= bits(r, 31, 31);
	c->writespeedfactor	= bits(r, 28, 26);
	c->writeblocklength	= bits(r, 25, 22);
	c->writeblockpartial	= bits(r, 21, 21);
	c->fileformatgroup	= bits(r, 15, 15);
	c->copy			= bits(r, 14, 14);
	c->permwriteprotect	= bits(r, 13, 13);
	c->tmpwriteprotect	= bits(r, 12, 12);
	c->fileformat		= bits(r, 11, 10);

	return 0;
}

char*
csdstr(char *p, char *e, Csd *c)
{
	char versbuf[128];

	versbuf[0] = '\0';
	if(c->version == 0)
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
		"version %x\n"
		"taac %x\n"
		"nsac %x\n"
		"xferspeed %x\n"
		"cmdclasses %x\n"
		"readblocklength %x\n"
		"readblockpartial %x\n"
		"writeblockmisalign %x\n"
		"readblockmisalign %x\n"
		"dsr %x\n"
		"devsize %x\n"
		"%s"
		"eraseblockenable %x\n"
		"erasesectorsize %x\n"
		"wpgroupsize %x\n"
		"wpgroupenable %x\n"
		"writespeedfactor %x\n"
		"writeblocklength %x\n"
		"writeblockpartial %x\n"
		"fileformatgroup %x\n"
		"copy %x\n"
		"permwriteprotect %x\n"
		"tmpwriteprotect %x\n"
		"fileformat %x\n",
		c->version,
		c->taac,
		c->nsac,
		c->xferspeed,
		c->cmdclasses,
		c->readblocklength,
		c->readblockpartial,
		c->writeblockmisalign,
		c->readblockmisalign,
		c->dsr,
		c->size,
		versbuf,
		c->eraseblockenable,
		c->erasesectorsize,
		c->wpgroupsize,
		c->wpgroupenable,
		c->writespeedfactor,
		c->writeblocklength,
		c->writeblockpartial,
		c->fileformatgroup,
		c->copy,
		c->permwriteprotect,
		c->tmpwriteprotect,
		c->fileformat);
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
		"rev %#ux\n"
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
