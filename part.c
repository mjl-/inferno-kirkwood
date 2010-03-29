#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"part.h"

static char Elongname[] = "partition name too long";

static ulong
g32(uchar *p)
{
	return (ulong)p[3]<<24 | (ulong)p[2]<<16 | (ulong)p[1]<<8 | (ulong)p[0]<<0;
}

static int
haspartname(Part *p, int np, char *s)
{
	int i;
	for(i = 0; i < np; i++)
		if(strcmp(p[i].name, s) == 0)
			return 1;
	return 0;
}

static void
partgenname(Part *pp, Part *parts, int nparts, char *first, char *next)
{
	int i;
	char *p = pp->name;
	char *e = p+sizeof pp->name;

	if(!haspartname(parts, nparts, first)) {
		if(seprint(p, e, "%s", first) == e)
			error(Elongname);
		return;
	}

	p = seprint(p, e, "%s.", next);
	for(i = 0;; i++) {
		if(seprint(p, e, "%d", i) == e)
			error(Elongname);
		if(!haspartname(parts, nparts, pp->name))
			break;
	}
}

static void
partname(Part *p, Part *parts, int nparts, int i)
{
	switch(p->typ) {
	case 0x39:	/* plan 9 */
		partgenname(p, parts, nparts, "plan9", "plan9");
		break;
	case 0x01:	/* dos fat-12 */
	case 0x04:	/* dos fat-16 */
	case 0x06:	/* dos > 32mb */
	case 0x0b:	/* fat32 */
	case 0x0c:	/* fat32-l */
	case 0x0e:	/* dos fat-16 */
		partgenname(p, parts, nparts, "9fat", "dos");
		break;
	default:
		snprint(p->name, sizeof p->name, "p%d", i);
	}
}

static void
partcheck(Part *p, Part *parts, int nparts, vlong size)
{
	int i;
	vlong js, je;

	if(p->e > size)
		error("partition beyond end of disk");
	if(p->s > p->e)
		error("bad partition, start > end");

	/* do not allow overlapping partitions, note that first Part is whole disk */
	for(i = 1; i < nparts; i++) {
		js = parts[i].s;
		je = parts[i].e;
		if(js >= p->s && js < p->e || je > p->s && je <= p->e)
			error("overlapping partitions");
	}
}

static void
partclear(Part *p)
{
	strcpy(p->uid, eve);
	p->perm = 0660;
	p->isopen = 0;
}

static Part*
partalloc(Part *p, int np)
{
	p = realloc(p, sizeof p[0]*(np+1));
	if(p == nil)
		error(Enomem);
	return p;
}

static void
printpartadd(Part *p)
{
	print("partadd, index %d, typ 0x%02ux, name %s, s %lld, e %lld, size %lld\n",
		(int)p->index, (uint)p->typ, p->name, p->s, p->e, p->size);
}

int
partinit(long (*r)(void *, int, void *, long, vlong), void *disk, vlong size, Part **partsp)
{
	uchar buf[512+63+1];
	uchar *p = (void*)(((ulong)buf+63)&~63);
	long n;
	int i, end, o;
	Part *pp;
	int nparts = 1;
	Part *parts = *partsp;
	char *s, *e;
	char *f[3];
	int nf;

	n = r(disk, 0, p, 512, 0);
	if(n != 512)
		error("reading mbr");

	if(p[510] != 0x55 || p[511] != 0xaa)
		error("missing mbr signature");

	/* partitions from mbr partition table */
	for(i = 0; i < 4; i++) {
		o = 446+i*16;

		*partsp = parts = partalloc(parts, nparts);
		pp = &parts[nparts];
		partclear(pp);
		pp->index = nparts;
		pp->typ = p[o+0x4];
		if(pp->typ == 0)
			continue;
		partname(pp, parts, nparts, i);
		pp->s = (vlong)g32(p+o+0x8)*512;
		pp->size = (vlong)g32(p+o+0xc)*512;
		pp->e = pp->s+pp->size;

		partcheck(pp, parts, nparts, size);
		nparts++;
		printpartadd(pp);
	}

	/* set up partitions in the plan 9 parts */
	end = nparts;
	for(i = 1; i < end; i++) {
		pp = &parts[i];
		if(pp->typ != 0x39 || pp->size < 2*512)
			continue;

		n = r(disk, 0, p, 512, pp->s+512);
		if(n != 512)
			error("reading plan 9 partition table");
		p[512] = 0;
		s = (char*)p;
		while(s < (char*)p+512) {
			e = strchr(s, '\n');
			if(e == nil)
				break;
			*e = 0;
			nf = tokenize(s, f, nelem(f));
			if(nf != 3)
				error("bad plan 9 partition table");
			s = e+1;

			*partsp = parts = partalloc(parts, nparts);
			pp = &parts[nparts];
			partclear(pp);
			pp->index = nparts;
			partgenname(pp, parts, nparts, f[0], f[0]);
			pp->s = (vlong)strtoull(f[1], nil, 0)*512;
			pp->e = (vlong)strtoull(f[2], nil, 0)*512;
			pp->size = pp->e-pp->s;
			partcheck(pp, parts, nparts, size);
			nparts++;
			printpartadd(pp);
		}
	}

	return nparts;
}
