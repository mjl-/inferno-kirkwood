/*
 * http://www.denx.de/wiki/view/DULG/UBootImages
 */

#include <lib9.h>
#include "uimage.h"

char *archs[] = {
"invalid",
"alpha",
"arm",
"386",
"ia64",
"mips",
"mips64",
"ppc",
"s390",
"sh",
"sparc",
"sparc64",
"m68k",
"nios",
"microblaze",
"nios2",
"blackfin",
"avr32",
"st200",
};

char *compressions[] = {
"none",
"gzip",
"bzip2",
};

char *types[] = {
"invalid",
"standalone",
"kernel",
"ramdisk",
"multi",
"firmware",
"script",
"filesystem",
"flatdt",
};

char *oses[] = {
"invalid",
"openbsd",
"netbsd",
"freebsd",
"44bsd",
"linux",
"srv4",
"esix",
"solaris",
"irix",
"sco",
"dell",
"ncr",
"lynxos",
"vxworks",
"psos",
"qnx",
"u-boot",
"rtems",
"artos",
"unity",
};


void
usage(void)
{
	fprint(2, "usage: mkuimage [-A arch] [-T type] [-C compression] [-O os] addr entry image [name]\n");
	exits("usage");
}


enum {
	Poly = 0xedb88320,
};
ulong crc32tab[256];

void
mkcrctab(void)
{
	int i, j;
	ulong c;

	for(i = 0; i < 256; i++) {
		c = i;
		for(j = 0; j < 8; j++)
			c = (c&1) ? Poly^(c>>1) : c>>1;
		crc32tab[i] = c;
	}
}

ulong
crc32(ulong crc, uchar *buf, ulong len)
{
	crc = crc^0xffffffffUL;
	while(len-- > 0)
		crc = crc32tab[((int)crc ^ (*buf++)) & 0xff] ^ (crc>>8);
	return crc^0xffffffffUL;
}


int
find(char *opts[], int nopts, char *s)
{
	int i;
	for(i = 0; i < nopts; i++)
		if(strcmp(s, opts[i]) == 0)
			return i;
	sysfatal("value %s not found", s);
	return -1;
}

int
p32(uchar *p, ulong v)
{
	p[0] = v>>24;
	p[1] = v>>16;
	p[2] = v>>8;
	p[3] = v>>0;
	return 4;
}

void
pack(Uhdr *h, char *pp)
{
	uchar *p;

	p = pp;
	memset(p, 0, UIMAGE_HDRSIZE);
	p += p32(p, h->magic);
	p += p32(p, h->hcrc);
	p += p32(p, h->time);
	p += p32(p, h->size);
	p += p32(p, h->load);
	p += p32(p, h->entry);
	p += p32(p, h->dcrc);
	*p++ = h->os;
	*p++ = h->arch;
	*p++ = h->type;
	*p++ = h->comp;
	strncpy(p, h->name, sizeof h->name);
}

void
main(int argc, char *argv[])
{
	char *image;
	Uhdr h;
	int fd;
	int n;
	char hdrbuf[UIMAGE_HDRSIZE];
	char *data;
	Dir *dir;

	h.magic = UIMAGE_MAGIC;

	h.arch = Aarm;
	h.type = Tkernel;
	h.comp = Cnone;
	h.os = Oopenbsd; /* abuse */

	ARGBEGIN {
	case 'A':
		h.arch = find(archs, nelem(archs), EARGF(usage()));
		break;
	case 'T':
		h.type = find(types, nelem(types), EARGF(usage()));
		break;
	case 'C':
		h.comp = find(compressions, nelem(compressions), EARGF(usage()));
		break;
	case 'O':
		h.os = find(oses, nelem(oses), EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND
	if(argc != 3 && argc != 4)
		usage();
	h.load = strtoul(argv[0], nil, 0);
	h.entry = strtoul(argv[1], nil, 0);
	image = argv[2];
	if(argv[3] != nil)
		strncpy(h.name, argv[3], sizeof h.name);
	else
		strncpy(h.name, image, sizeof h.name);

	h.time = time(0);

	fd = open(image, OREAD);
	if(fd < 0)
		sysfatal("open %s: %r", image);
	dir = dirfstat(fd);
	if(dir == nil)
		sysfatal("dirfstat: %r");
	h.size = (ulong)dir->length;

	data = malloc(h.size);
	if(data == nil)
		sysfatal("malloc: %r");
	n = readn(fd, data, h.size);
	if(n != h.size)
		sysfatal("failed/short read");

	mkcrctab();
	h.dcrc = crc32(0, data, h.size);
	h.hcrc = 0;
	pack(&h, hdrbuf);
	h.hcrc = crc32(0, hdrbuf, sizeof hdrbuf);
	pack(&h, hdrbuf);

	if(write(1, hdrbuf, sizeof hdrbuf) != sizeof hdrbuf)
		sysfatal("writing header: %r");
	if(write(1, data, h.size) != h.size)
		sysfatal("writing image: %r");
	if(close(fd) < 0)
		sysfatal("close: %r");
	exits(0);
}
