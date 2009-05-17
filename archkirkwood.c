#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

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
