enum {
	UIMAGE_MAGIC =		0x27051956,
	UIMAGE_NAMELEN =	32,
	UIMAGE_HDRSIZE =	0x40,
};

typedef struct Uhdr Uhdr;
struct Uhdr
{
	ulong	magic;
	ulong	hcrc;
	ulong	time;
	ulong	size;
	ulong	load;
	ulong	entry;
	ulong	dcrc;
	uchar	os;
	uchar	arch;
	uchar	type;
	uchar	comp;
	uchar	name[UIMAGE_NAMELEN];
};

/* architectures */
enum {
	Ainvalid,
	Aalpha,
	Aarm,
	A386,
	Aia64,
	Amips,
	Amips64,
	Appc,
	As390,
	Ash,
	Asparc,
	Asparc64,
	Am68k,
	Anios,
	Amicroblaze,
	Anios2,
	Ablackfin,
	Aavr32,
	Ast200,
};

/* compression types for payload */
enum {
	Cnone,
	Cgzip,
	Cbzip2,
};

/* uimage types */
enum {
	Tinvalid,
	Tstandalone,
	Tkernel,
	Tramdisk,
	Tmulti,
	Tfirmware,
	Tscript,
	Tfilesystem,
	Tflatdt,
};

/* operating systems.  inferno & plan 9 are missing */
enum {
	Oinvalid,
	Oopenbsd,
	Onetbsd,
	Ofreebsd,
	O44bsd,
	Olinux,
	Osrv4,
	Oesix,
	Osolaris,
	Oirix,
	Osco,
	Odell,
	Oncr,
	Olynxos,
	Ovxworks,
	Opsos,
	Oqnx,
	Ouboot,
	Ortems,
	Oartos,
	Ounity,
};
