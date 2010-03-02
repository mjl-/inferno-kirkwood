/*
nand flash for the sheevaplug.
for now separate from os/port/flashnand.c because the flash seems
newer, has different commands.  but that is nand-chip specific, not
sheevaplug-specific.  should be merged in future.

the sheevaplug has a hynix 4gbit flash chip: hy27uf084g2m.
2048 byte pages, with 64 spare bytes each.
erase block size is 128k.

it has a "glueless" interface, at 0xf9000000.  that's the address
of the data register.  the command and address registers are those
or'ed with 0x1 and 0x2 respectively.

linux uses this layout for the nand flash (from address 0 onwards):
- 1mb for u-boot
- 4mb for kernel
- 507mb for file system

this is not so relevant here except for ecc.  the first two areas
(u-boot and kernel) are excepted to have 4-bit ecc per 512 bytes
(but calculated from last byte to first), bad erase blocks skipped.
the file system area has 1-bit ecc per 256 bytes.

todo:
- do not hard code the page sizes & delays
- allow subpage writing.
- allow random reading.
- do caching read/write for faster sequential operation.
- figure out interface that allows reading out of band data?  those extra 64 bytes shift the offsets.  mostly useful for supporting different styles of ecc or other meta data.
- is setting NandActCEBoot necessary?
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

#include	"../port/flashif.h"
#include	"nandecc.h"

enum {
	NandActCEBoot	= 1<<1,
};

/* vendors */
enum {
	Hynix		= 0xad,
};

/* chips */
enum {
	Hy27UF084G2M	= 0xdc,
};

typedef struct Nandtab Nandtab;
struct Nandtab {
	int	vid;
	int	did;
	vlong	size;
	char*	name;
};
Nandtab nandtab[] = {
	{Hynix,		Hy27UF084G2M,	512*1024*1024,	"Hy27UF084G2M"},
};

/* commands */
enum {
	Readstatus	= 0x70,
	Readid		= 0x90,	/* needs one 0 address write */
	Reset		= 0xff,

	Read		= 0x00,	/* needs five address writes followed by Readstart, Readstartcache or Restartcopy */
	Readstart	= 0x30,
	Readstartcache	= 0x31,
	Readstartcopy	= 0x35,
	Readstopcache	= 0x34, /* after Readstartcache, to stop reading next pages */

	Program		= 0x80,	/* needs five address writes, the data, and -start or -cache */
	Programstart	= 0x10,
	Programcache	= 0x15,

	Copyback	= 0x85,	/* followed by Programstart */

	Erase		= 0x60,	/* three address writes for block followed by Erasestart */
	Erasestart	= 0xd0,

	Randomread	= 0x85,
	Randomwrite	= 0x05,
	Randomwritestart= 0xe0,
};

/* status bits */
enum {
	SFail		= 1<<0,
	SCachefail	= 1<<1,
	SIdle		= 1<<5,
	SReady		= 1<<6,
	SNotprotected	= 1<<7,
};

static void
nandcmd(Flash *f, uchar b)
{
	volatile uchar *p = (void*)((ulong)f->addr|1);
	*p = b;
}

static void
nandaddr(Flash *f, uchar b)
{
	volatile uchar *p = (void*)((ulong)f->addr|2);
	*p = b;
}

static uchar
nandread(Flash *f)
{
	volatile uchar *p = f->addr;
	return *p;
}

static void
nandreadn(Flash *f, uchar *buf, long n)
{
	volatile uchar *p = f->addr;
	while(n-- > 0)
		*buf++ = *p;
}

static void
nandwrite(Flash *f, uchar b)
{
	volatile *p = (void*)f->addr;
	*p = b;
}

static void
nandwriten(Flash *f, uchar *buf, long n)
{
	volatile uchar *p = f->addr;
	while(n-- > 0)
		*p = *buf++;
}

static void
nandclaim(Flash*)
{
	NANDFREG->ctl |= NandActCEBoot;
}

static void
nandunclaim(Flash*)
{
	NANDFREG->ctl &= ~NandActCEBoot;
}

static int
idchip(Flash *f)
{
	int i;
	Flashregion *r;
	uchar maker, device, id3, id4;
	int pagesizes[] = {1024, 2*1024, 0, 0};
	int blocksizes[] = {64*1024, 128*1024, 256*1024, 0};
	int spares[] = {8, 16}; /* per 512 bytes */
	Nandtab *chip;
	int spare;

	f->id = 0;
	f->devid = 0;
	f->width = 1;
	nandclaim(f);
	nandcmd(f, Readid);
	nandaddr(f, 0);
	maker = nandread(f);
	device = nandread(f);
	id3 = nandread(f);
	USED(id3);
	id4 = nandread(f);
	nandunclaim(f);

	//iprint("man=%#ux device=%#ux id3=%#ux id4=%ux\n", maker, device, id3, id4);
	if(maker != Hynix) {
		print("nand: unknown vendor %#ux\n", maker);
		return -1;
	}
	for(i = 0; i < nelem(nandtab); i++) {
		chip = &nandtab[i];
		if(chip->vid == maker && chip->did == device) {
			f->id = maker;
			f->devid = device;
			f->width = 1;
			f->nr = 1;
			f->size = chip->size;

			r = &f->regions[0];
			r->pagesize = pagesizes[id4&3];
			r->erasesize = blocksizes[(id4>>4)&3];
			r->n = f->size/r->erasesize;
			r->start = 0;
			r->end = f->size;

			spare = r->pagesize/512*spares[(id4>>2)&1];
			print("kwnand: %s, size %ludKB\n", chip->name, f->size/1024);
			print("        pagesize %lud, erasesize %lud, spare per page %d\n", r->pagesize, r->erasesize, spare);
			return 0;
		}
	}
	print("nand: device %#.2ux/%#.2ux not recognised\n", maker, device);
	return -1;
}

static int
erasezone(Flash *f, Flashregion *r, ulong offset)
{
	int i;
	uchar s;
	ulong page, block;

	print("erasezone, offset %#lux, region nblocks %d, start %#lux, end %#lux\n", offset, r->n, r->start, r->end);
	print(" erasesize %lud, pagesize %lud\n", r->erasesize, r->pagesize);

	if(offset & ((128*1024)-1)) {
		print("not block aligned\n");
		return -1;
	}
	page = offset>>11;
	block = page>>6;
	print("erase, block %#lux\n", block);

	/* make sure controller is idle */
	nandclaim(f);
	nandcmd(f, Readstatus);
	if((nandread(f) & (SIdle|SReady)) != (SIdle|SReady)) {
		nandunclaim(f);
		print("erase, flash busy\n");
		return -1;
	}

	/* start erasing */
	nandcmd(f, Erase);
	nandaddr(f, page>>0);
	nandaddr(f, page>>8);
	nandaddr(f, page>>16);
	nandcmd(f, Erasestart);

	/* have to wait until flash is done.  typically ~2ms */
	delay(1);
	nandcmd(f, Readstatus);
	for(i = 0; i < 100; i++) {
		s = nandread(f);
		if(s & SReady) {
			nandunclaim(f);
			if(s & SFail) {
				print("erase failed, block %#lux\n", block);
				return -1;
			}
			return 0;
		}
		microdelay(50);
	}
	print("erase timeout, block %#lux\n", block);
	nandunclaim(f);
	return -1;
}

static int
write(Flash *f, ulong offset, void *buf, long n)
{
	uchar *p;
	uchar oob[64];
	uchar *eccp;
	ulong v;
	ulong page;
	int i;
	uchar s;

	page = offset>>11;

print("write, nand, offset %#lux, page %#lux\n", offset, page);

	if(n != 2048) {
		print("write, can only write whole pages\n");
		return -1;
	}

	if(offset & (2048-1)) {
		print("write, offset not page aligned\n");
		return -1;
	}

	p = buf;
	memset(oob, 0xff, sizeof oob);
	eccp = oob+64-24;
	for(i = 0; i < 2048/256; i++) {
		v = nandecc(p);
		eccp[0] = v>>8;
		eccp[1] = v>>0;
		eccp[2] = v>>16;

		p += 256;
		eccp += 3;
	}

	nandclaim(f);

	nandcmd(f, Readstatus);
	if((nandread(f) & (SIdle|SReady)) != (SIdle|SReady)) {
		nandunclaim(f);
		print("write, nand not ready & idle\n");
		return -1;
	}

	/* write, only whole pages for now, no sub-pages */
	nandcmd(f, Program);
	nandaddr(f, 0);
	nandaddr(f, 0);
	nandaddr(f, page>>0);
	nandaddr(f, page>>8);
	nandaddr(f, page>>16);
	nandwriten(f, buf, 2048);
	nandwriten(f, oob, 64);
	nandcmd(f, Programstart);

	microdelay(100);
	nandcmd(f, Readstatus);
	for(i = 0; i < 100; i++) {
		s = nandread(f);
		if(s & SReady) {
			nandunclaim(f);
			if(s & SFail) {
				print("write failed, page %#lux\n", page);
				return -1;
			}
			return 0;
		}
		microdelay(10);
	}

	nandunclaim(f);
	print("write, timeout for page %#lux\n", page);
	return -1;
}

static int
read(Flash *f, ulong offset, void *buf, long n)
{
	ulong addr, page;
	Flashregion *r = &f->regions[0];
	uchar *p;
	ulong v, w;
	uchar *eccp;
	uchar oob[64];
	int i;

	addr = offset&(r->pagesize-1);
	page = offset>>11;
	if(addr != 0 || n != 2048) {
		print("read, must read one page at a time\n");
		return -1;
	}

	print("kwnand, read, offset %#lux, addr %#lux, page %#lux\n", offset, addr, page);

	nandclaim(f);
	nandcmd(f, Read);
	nandaddr(f, addr>>0);
	nandaddr(f, addr>>8);
	nandaddr(f, page>>0);
	nandaddr(f, page>>8);
	nandaddr(f, page>>16);
	nandcmd(f, Readstart);

	microdelay(50);

	nandreadn(f, buf, n);
	nandreadn(f, oob, 64);

	nandunclaim(f);

	if(n == 2048) {
		/* verify/correct data.  last 8*3 bytes is ecc, per 256 bytes. */
		p = buf;
		eccp = oob+64-24;
		for(i = 0; i < 2048/256; i++) {
			v = nandecc(p);
			w = 0;
			w |= eccp[0]<<8;
			w |= eccp[1]<<0;
			w |= eccp[2]<<16;
			switch(nandecccorrect(p, v, &w, 1)) {
			case NandEccErrorBad:
				print("(page %d)\n", i);
				return -1;
			case NandEccErrorOneBit:
			case NandEccErrorOneBitInEcc:
				print("(page %d)\n", i);
			case NandEccErrorGood:
				break;
			}

			eccp += 3;
			p += 256;
		}
	}

	return 0;
}

static int
reset(Flash *f)
{
iprint("kwnandreset\n");
	if(f->data != nil)
		return 1;
	f->write = write;
 	f->read = read;
	f->eraseall = nil;
	f->erasezone = erasezone;
	f->suspend = nil;
	f->resume = nil;
	f->sort = "nand";
	return idchip(f);
}

void
kwnandlink(void)
{
	addflashcard("nand", reset);
}
