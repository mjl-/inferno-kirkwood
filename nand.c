#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

#include	"../port/flashif.h"

/*
 * Sheevaplug-specific NAND flash interface: : HY27US08121A
 * (Hynix NAND 512MiB 3,3V 8-bit) id: 0xad 0xdc 0x80 0x95 
 */

enum {
	MaskCLE	= 0x01,
	MaskALE	= 0x02,

	NandActCEBoot	= 0x02,

	Identify = 0x90
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

	v = (ulong)f->addr & ~(MaskCLE|MaskALE);

	if(cle&ale)
		return;	/* ignore ambiguous operation */
	if(cle)
		v |= MaskCLE;
	if(ale)
		v |= MaskALE;

	f->addr = KADDR(v);
}

/*
 * TODO: perform 4-byte reads
 * could unroll the loops
 */

void
archnand_read(Flash *f, void *buf, int len)
{
	uchar *p, *bp;

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

void
archnand_write(Flash *f, void *buf, int len)
{
	uchar *p, *bp;

	p = f->addr;
	bp = buf;
	while(--len >= 0)
		*p = *bp++;
}
