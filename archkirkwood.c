#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"
#include "../port/netif.h"
#include "etherif.h"

static char *
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
	m->cpuhz = 1200*1000*1000;
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
		strcpy(e->type, "kirkwood");
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
		return 1;
	}
	return -1;
}

static int watchdoghz = 0; // enable with 1 (hz)

static void
wdogclock(void)
{
	/* update watchdog */
	TIMERREG->timerwd = CLOCKFREQ/watchdoghz;
}

/* LED/USB gpios */
enum
{
	SheevaOEValLow	= 1<<29,        /* USB_PWEN low */
	SheevaOEValHigh	= 1<<17,        /* LED pin high */
	SheevaOELow	= ~0,
	SheevaOEHigh	= ~0,
};

static void
gpioconf(ulong gpp0_oe_val, ulong gpp1_oe_val, ulong gpp0_oe, ulong gpp1_oe)
{
	GPIO0REG->dataout = gpp0_oe_val;
	GPIO0REG->dataoutena = gpp0_oe;

	GPIO1REG->dataout= gpp1_oe_val;
	GPIO1REG->dataoutena= gpp1_oe;
}


void
archreset(void)
{
	gpioconf(SheevaOEValLow, SheevaOEValHigh, SheevaOELow, SheevaOEHigh);

	/* reset devices to initial state */
	if(watchdoghz){
		addclock0link(wdogclock, watchdoghz);
		TIMERREG->timerwd = CLOCKFREQ/watchdoghz;
		TIMERREG->ctl |= TmrWDenable;
		CPUCSREG->rstout |= RstoutWatchdog;
	}
}

void
archreboot(void)
{
	CPUCSREG->rstout = RstoutSoft;
	CPUCSREG->softreset = ResetSystem;
}


void
archconsole(void)
{
	uartconsole();
}

int
pcmspecial(char *idstr, ISAConf *isa)
{
	return -1;
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
