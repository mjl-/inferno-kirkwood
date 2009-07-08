#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

#include	"../port/flashif.h"

/*
 * Sheevaplug-specific NAND flash interface: HY27UF084G2M
 * (Hynix NAND 512MiB 3,3V 8-bit) id: 0xad 0xdc 0x80 0x95 
 */

enum {
	NandCLE	= 0x01,
	NandALE	= 0x02,

	NandActCEBoot	= 0x02,
};

void
archnand_init(Flash*)
{
}

void
archnand_claim(Flash*, int claim)
{
	if (claim)
		NANDFREG->ctl |= NandActCEBoot;
	else
		NANDFREG->ctl &= ~NandActCEBoot;
}

void
archnand_setCLEandALE(Flash* f, int cle, int ale)
{
	ulong v;

	v = (ulong)f->addr & ~(NandCLE|NandALE);

	if(cle&ale)
		return;	/* ignore ambiguous operation */
	if(cle)
		v |= NandCLE;
	if(ale)
		v |= NandALE;

	f->addr = KADDR(v);
}

/*
 * TODO: (word/byte) reads until 64 bytes chunks (duffdev?)
 */

void
archnand_read(Flash *f, void *buf, int len)
{
	uchar *p, *bp;

//	print("nread %lux %d\n", f->addr, len);
	p = f->addr;
	if(buf != nil){
		bp = buf;
		while(--len >= 0)
			*bp++ = *p;
	}else{
		int junk;
		while(--len >= 0){
			junk = *p;
			USED(junk);
		}
	}
}

/*
 * TODO use duffdev (word/byte access?)
 */

void
archnand_write(Flash *f, void *buf, int len)
{
	uchar *p, *bp;

//	print("nwrite %lux %d\n", f->addr, len);
	p = f->addr;
	bp = buf;
	while(--len >= 0)
		*p = *bp++;
}
