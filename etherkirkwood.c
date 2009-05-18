#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "etherif.h"

static int
kirkwoodpnp(Ether*)
{
	return -1;
}

void
etherkirkwoodlink(void)
{
	addethercard("kirkwood", kirkwoodpnp);
}
