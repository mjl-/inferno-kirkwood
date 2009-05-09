#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

void
archconfinit(void)
{
	conf.topofmem = 512*1024*124;
	m->cpuhz = 100;
}

void
archreset(void)
{
}

void
archreboot(void)
{
	/* xxx */
}
