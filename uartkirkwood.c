#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/uart.h"

extern PhysUart kirkwoodphysuart;

PhysUart kirkwoodphysuart = {
	.name		= "kirkwood",
	/* xxx */
};
