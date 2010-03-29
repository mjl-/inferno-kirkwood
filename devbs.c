/*
 * hot-pluggable block storage devices
 *
 * todo:
 * - locking
 * - understand extended partitions
 * - hotplug & events, make event-file per Qdiskpart too.
 * - think about returning a type of device, eg "ata", "sdcard"
 * - think about raw command access.
 * - think about allowing setting of block size and required alignment.
 * - add hooks so devices can tell they have gone/arrived.
 * - invalidate open files after reinit of device.
 * - think of way to update the partition table.  for now, write to the data file, then reinit the device.
 * - wstat, to change owner/perm
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"part.h"
#include	"bs.h"

static char Enodisk[] = "no such disk";
static char Enopart[] = "no such partition";

#define QTYPE(v)	((int)((v)>>0 & 0xff))
#define QDISK(v)	((int)((v)>>8 & 0xff))
#define QPART(v)	((int)((v)>>16 & 0xff))
#define QPATH(p,d,t)	((p)<<16 | (d)<<8 | (t)<<0)
enum{
	Qdir, Qbsctl, Qbsevent,
	Qdiskdir, Qdiskctl, Qdiskdevctl, Qdiskraw, Qdiskevent, Qdiskpart,
};
static
Dirtab bstab[] = {
	".",		{Qdir,0,QTDIR},		0,	0555,
	"bsctl",	{Qbsctl},		0,	0660,
	"bsevent",	{Qbsevent},		0,	0440,
	"xxx",		{Qdiskdir,0,QTDIR},	0,	0555,
	"ctl",		{Qdiskctl},		0,	0660,
	"devctl",	{Qdiskdevctl},		0,	0660,
	"raw",		{Qdiskraw},		0,	0660,
	"event",	{Qdiskevent},		0,	0440,
	"xxx",		{Qdiskpart},		0,	0660,
};

static QLock disksl;
static Store **disks = nil;	/* indexed by Store.num */
static int maxdisk = 0;	/* highest index of disk */

static Store*
diskget(int num)
{
	if(num >= 0 && num <= maxdisk && disks[num] != nil)
		return disks[num];
	return nil;
}

static Store*
xdiskget(int num)
{
	Store *d;
	d = diskget(num);
	if(d == nil)
		error(Enodisk);
	return d;
}

static void
partdata(Part *p, vlong size)
{
	p->index = 0;
	p->typ = 0;
	strcpy(p->name, "data");
	p->s = 0;
	p->e = size;
	p->size = size;
	strcpy(p->uid, eve);
	p->perm = 0660;
	p->isopen = 0;
}

static void
diskinit(int num)
{
	Store *d;

	d = diskget(num);
	if(d == nil)
		error(Enodisk);
	d->diskinit(d);

	d->parts = smalloc(sizeof d->parts[0]);
	partdata(&d->parts[0], d->size);
	if(waserror()) {
		free(d->parts);
		d->parts = nil;
		nexterror();
	}
	d->nparts = partinit(d->io, d, d->size, &d->parts);
	if(d->nparts < 0)
		error("partinit");
	poperror();

	d->num = num;
}

static Part*
partget(Store *d, int i)
{
	if(i < 0 || i >= d->nparts)
		return nil;
	return &d->parts[i];
}

static Part*
xpartget(Store *d, int i)
{
	Part *p;
	p = partget(d, i);
	if(p == nil)
		error(Enopart);
	return p;
}

static long
io(Store *d, Part *p, int iswrite, void *buf, long n, vlong off)
{
	vlong s, e;

	if(off < 0 || n < 0)
		error(Ebadarg);
	s = off;
	e = off+n;
	if(s > p->size)
		s = p->size;
	if(e > p->size)
		e = p->size;
	s += p->s;
	e += p->s;
	return d->io(d, iswrite, buf, e-s, s);
}

static int
bsgen(Chan *c, char *, Dirtab *, int, int i, Dir *dp)
{
	Dirtab *t;
	Store *d;
	Part *p;

//print("bsgen, c->qid.path %#llux, i %d\n", c->qid.path, i);

	if(i == DEVDOTDOT) {
		switch(QTYPE(c->qid.path)) {
		case Qdir:
		case Qdiskdir:
			t = &bstab[Qdir];
			devdir(c, t->qid, t->name, t->length, eve, t->perm, dp);
			return 1;
		}
		panic("devdotdot on non-dir qid.path %#llux\n", c->qid.path);
	}

	switch(QTYPE(c->qid.path)) {
	default:
		panic("bsgen on non-dir qid.path %#llux\n", c->qid.path);

	case Qdir:
	case Qbsctl:
	case Qbsevent:
		if(i == 0 || i == 1) {
			t = &bstab[Qbsctl+i];
			devdir(c, t->qid, t->name, t->length, eve, t->perm, dp);
			return 1;
		}
		i -= 2;
		if(i > maxdisk)
			return -1;
		d = diskget(i);
		if(d == nil)
			return 0;
		t = &bstab[Qdiskdir];
		devdir(c, (Qid){QPATH(0,d->num,Qdiskdir),0,QTDIR}, d->name, t->length, eve, t->perm, dp);
		return 1;

        case Qdiskdir:
	case Qdiskctl:
	case Qdiskdevctl:
	case Qdiskraw:
	case Qdiskevent:
	case Qdiskpart:
		d = diskget(QDISK(c->qid.path));
		if(d == nil)
			return -1;
		if(i >= 0 && i < Qdiskevent-Qdiskctl+1) {
			t = &bstab[Qdiskctl+i];
			devdir(c, (Qid){QPATH(0,d->num,Qdiskctl+i),0,QTFILE}, t->name, t->length, eve, t->perm, dp);
			return 1;
		}
		i -= Qdiskevent-Qdiskctl+1;
		if(i >= d->nparts)
			return -1;
		p = &d->parts[i];
		devdir(c, (Qid){QPATH(i,d->num,Qdiskpart),0,QTFILE}, p->name, p->size, p->uid, p->perm, dp);
		return 1;
	}
	return -1;
}


static void
bsreset(void)
{
	print("bsreset\n");
}

static void
bsinit(void)
{
	int i;
	Store *d;

	for(i = 0; i <= maxdisk; i++) {
		d = diskget(i);
		if(d == nil)
			continue;
		if(waserror()) {
			// xxx  mark disk as bad/not yet initialized
			continue;
		}
		d->init(d);
		poperror();
	}
}

static Chan*
bsattach(char* spec)
{
	return devattach('n', spec);
}

static Walkqid*
bswalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, bsgen);
}

static int
bsstat(Chan* c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, bsgen);
}

static Chan*
bsopen(Chan* c, int omode)
{
	Store *d;
	Part *p = nil;

	if(QTYPE(c->qid.path) == Qdiskpart) {
		d = xdiskget(QDISK(c->qid.path));
		p = xpartget(d, QPART(c->qid.path));
		if(p->index == 0)
			p = nil;
		else if(p->isopen)
			error(Einuse);
	}
	c = devopen(c, omode, nil, 0, bsgen);
	if(p != nil)
		p->isopen++;
	return c;
}

static int
bswstat(Chan* c, uchar *dp, int n)
{
	USED(c, dp);
	error(Eperm);
	return n;
}

static void
bsclose(Chan* c)
{
	Store *d;
	Part *p;

	if(QTYPE(c->qid.path) == Qdiskpart && c->flag&COPEN && QPART(c->qid.path) != 0) {
		d = xdiskget(QDISK(c->qid.path));
		p = xpartget(d, QPART(c->qid.path));
		if(p->isopen != 1)
			panic("devclose on part.isopen != 1");
		p->isopen = 0;
	}
}

static long
bsread(Chan* c, void* a, long n, vlong off)
{
	Store *d;
	Part *p;

	switch(QTYPE(c->qid.path)){
	case Qdir:
	case Qdiskdir:
		return devdirread(c, a, n, nil, 0, bsgen);
	case Qbsctl:
		// xxx should print info about initialized disks.  perhaps only registered too.
		error("not yet");
	case Qbsevent:
		error("not yet");
	case Qdiskctl:
		error("not yet");
	case Qdiskdevctl:
		d = xdiskget(QDISK(c->qid.path));
		p = xpartget(d, 0);
		devpermcheck(p->uid, p->perm, OREAD);
		n = d->rctl(d, a, n, off);
		break;
	case Qdiskraw:
		error("not implemented");
	case Qdiskevent:
		error("not yet");
	case Qdiskpart:
		d = xdiskget(QDISK(c->qid.path));
		p = xpartget(d, QPART(c->qid.path));
		n = io(d, p, 0, a, n, off);
		break;

	default:
		n = 0;
		break;
	}
	return n;
}

enum {
	CMinit,
};
static Cmdtab bsctl[] = {
	CMinit,		"init",		2,
};
enum {
	CMdiskinit,
};
static Cmdtab bsdiskctl[] = {
	CMdiskinit,	"init",		1,
};

static long
bswrite(Chan* c, void* a, long n, vlong off)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	Store *d;
	Part *p;
	int num;

	qlock(&disksl);
	if(waserror()) {
		qunlock(&disksl);
		nexterror();
	}

	switch(QTYPE(c->qid.path)){
	case Qbsctl:
		if(!iseve())
			error(Eperm);

		cb = parsecmd(a, n);
		if(waserror()) {
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, bsctl, nelem(bsctl));
		switch(ct->index) {
		case CMinit:
			num = atoi(cb->f[1]);
			diskinit(num);
			break;
		}
		poperror();
		free(cb);
		break;

	case Qbsevent:
		error("not yet");

	case Qdiskctl:
		d = xdiskget(QDISK(c->qid.path));
		p = partget(d, 0);
		if(p != nil)
			devpermcheck(p->uid, p->perm, OWRITE);
		else if(!iseve())
			error(Eperm);

		cb = parsecmd(a, n);
		if(waserror()) {
			free(cb);
			nexterror();
		}
		ct = lookupcmd(cb, bsdiskctl, nelem(bsdiskctl));
		switch(ct->index) {
		case CMdiskinit:
			diskinit(d->num);
			break;
		}
		poperror();
		free(cb);
		break;

	case Qdiskdevctl:
		if(off != 0)
			error(Ebadarg);
		d = xdiskget(QDISK(c->qid.path));
		p = xpartget(d, 0);
		devpermcheck(p->uid, p->perm, OWRITE);
		n = d->wctl(d, a, n);
		break;

	case Qdiskraw:
		error("not implemented");

	case Qdiskpart:
		d = xdiskget(QDISK(c->qid.path));
		p = xpartget(d, QPART(c->qid.path));
		n = io(d, p, 0, a, n, off);
		break;
	default:
		error(Ebadusefd);
	}

	poperror();
	qunlock(&disksl);
	return n;
}

Dev bsdevtab = {
	'n',
	"bs",

	bsreset,
	bsinit,
	devshutdown,
	bsattach,
	bswalk,
	bsstat,
	bsopen,
	devcreate,
	bsclose,
	bsread,
	devbread,
	bswrite,
	devbwrite,
	devremove,
	bswstat,
};

void
blockstoreadd(Store *d)
{
	qlock(&disksl);
	if(d->num > 99)
		panic("bad block store num %d", d->num);
	if(diskget(d->num) != nil)
		panic("disk.num %d already registered", d->num);

	if(d->num > maxdisk) {
		disks = realloc(disks, sizeof disks[0]*(d->num+1));
		if(disks == nil)
			panic("no memory");
		while(maxdisk < d->num)
			disks[maxdisk++] = nil;
	}
		
	disks[d->num] = d;
	snprint(d->name, sizeof d->name, "bs%02d", d->num);
	qunlock(&disksl);
}
