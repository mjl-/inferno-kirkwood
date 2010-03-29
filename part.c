#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"part.h"

static ulong
g32(uchar *p)
{
	return (ulong)p[3]<<24 | (ulong)p[2]<<16 | (ulong)p[1]<<8 | (ulong)p[0]<<0;
}

int
partinit(long (*r)(void *, int, void *, long, vlong), void *disk, Part **partsp)
{
	uchar buf[512+63];
	uchar *p = (void*)(((ulong)buf+63)&~63);
	long n;
	int i, o;
	Part part;
	int nparts = 1;
	Part *parts = *partsp;

	n = r(disk, 0, p, 512, 0);
	if(n != 512)
		error("reading mbr");

	if(p[510] != 0x55 || p[511] != 0xaa)
		error("missing mbr signature");

	for(i = 0; i < 4; i++) {
		o = 446+i*16;
		part.typ = p[o+0x4];
		if(part.typ == 0)
			continue;
		part.index = nparts;
		snprint(part.name, sizeof part.name, "p%02d", i);
		part.s = (vlong)g32(p+o+0x8)*512;
		part.size = (vlong)g32(p+o+0xc)*512;
		part.e = part.s+part.size;
		strcpy(part.uid, eve);
		part.perm = 0660;
		print("part, index %d, typ 0x%ux, name %s, s %lld, e %lld, size %lld\n",
			(int)part.index, (uint)part.typ, part.name, part.s, part.e, part.size);
		parts = realloc(parts, sizeof parts[0]*(nparts+1));
		if(parts == nil)
			error(Enomem);
		*partsp = parts;
		memmove(&parts[nparts++], &part, sizeof part);
	}

	return nparts;
}
