#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/uart.h"

PhysUart kirkwoodphysuart = {
	.name		= "kirkwood",
};

void
delay(void)
{
	int i;

	i = 0;
	while(i < (1<<17))
		i++;
}

void
putc(int c)
{
	UartReg *r = UART0REG;

	while((r->lsr&LSRthre) == 0)
		delay();
	r->thr = c;
}

void
puts(char *data, int len)
{
	int s;

//	if(!uartspcl && !redirectconsole)
//		return;
	s = splhi();
	while(--len >= 0){
		if(*data == '\n')
			putc('\r');
		putc(*data++);
	}
	splx(s);
}

void
uartconsole(void)
{
	/* TODO
	 * see manga/uart*.c to setup uart in order 
	 * to user uart{getc, putc, puts} in devuart.c.
	 *
	 * As soon as uartputs starts working the
	 * print/iprint functions from devcons.c should be usable.
	 */
}
