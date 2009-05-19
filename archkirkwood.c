#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "../port/netif.h"
#include "etherif.h"

char *
devidstr(ulong v)
{
	switch(v) {
	case 0x0:	return "88F6180";
	case 0x1:	return "88F619[02]";
	case 0x2:	return "88F6281";
	}
	return "unknown";
}

void
archconfinit(void)
{
	conf.topofmem = 512*1024*124;
	m->cpuhz = 100;
	conf.devidstr = devidstr(*(ulong*)AddrDevid);
}

static void
p16(uchar *p, ulong v)
{
	*p++ = v>>8;
	*p++ = v>>0;
}

static void
p32(uchar *p, ulong v)
{
	*p++ = v>>24;
	*p++ = v>>16;
	*p++ = v>>8;
	*p++ = v>>0;
}

int
archether(int ctlno, Ether *e)
{
	GbeReg* gbe0 = GBE0REG;
	ulong ps0;

	switch(ctlno) {
	case 0:
		e->itype = Irqlo;
		e->irq = IRQ0gbe0sum;
		e->mem = AddrGbe0;
		p32(e->ea, gbe0->macah);
		p16(e->ea+4, gbe0->macal);
		print("ether, mac %2.2ux%2.2ux%2.2ux%2.2ux%2.2ux%2.2ux",
			e->ea[0], e->ea[1], e->ea[2], e->ea[3], e->ea[4], e->ea[5]);
		ps0 = gbe0->ps0;
		print(", link %s, %s duplex, speed %s, flow control %s\n",
			(ps0&(1<<1)) ? "up" : "down",
			(ps0&(1<<2)) ? "full" : "half",
			(ps0&(1<<4)) ? "1000" : "10/100",
			(ps0&(1<<3)) ? "on" : "off"
		);
		e->nopt = 0;
		break;
	default:
		return -1;
	}
print("xxx ether\n");
	return -1;
}

void
archreset(void)
{
}

void
archreboot(void)
{
}

void
archconsole(void)
{
	uartconsole();
}

void
kbdinit(void)
{
}

uvlong
fastticks(uvlong *hz)
{
	if(hz)
		*hz = HZ;
	return m->ticks;
}

