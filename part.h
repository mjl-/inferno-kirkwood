typedef struct Part Part;

struct Part
{
	uchar	index;		/* in partition table */
	uchar	typ;		/* type from partition table */
	char	name[32];	/* name, "p%02d" for those from mbr */
	vlong	s;		/* start,end,size in bytes */
	vlong	e;
	vlong	size;

	char	uid[KNAMELEN];
	ulong	perm;
};

int partinit(long (*read)(void *disk, int iswrite, void *buf, long n, vlong off), void *disk, Part **parts);
