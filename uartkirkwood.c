#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/uart.h"

static void delay(void); // xxx get rid of this

enum {
	UartFREQ =	0, // xxx
};

extern PhysUart kirkwoodphysuart;

typedef struct Ctlr Ctlr;
struct Ctlr {
	UartReg*	regs;
	int	irq;
	Lock;
};

static Ctlr kirkwoodctlr[] = {
{       .regs   = UART0REG,
        .irq    = IRQ1uart0, },
};


static Uart kirkwooduart[] = {
{	.regs	= &kirkwoodctlr[0],
	.name	= "eia0",
	.freq	= UartFREQ,
	.phys	= &kirkwoodphysuart,
	.special= 0,
	.next	= nil, },
};


static void
kw_read(Uart *uart)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	ulong lsr;
	char c;

	for(;;) {
		lsr = regs->lsr;
		if((lsr&LSRrx) == 0)
			break;

                if(lsr&LSRrunerr)
                        uart->oerr++;
                if(lsr&LSRparerr)
                        uart->perr++;
                if(lsr&LSRframeerr)
                        uart->ferr++;

		c = (char)regs->rbr;
		if((lsr & (LSRbi|LSRframeerr|LSRparerr)) == 0)
			uartrecv(uart, c);
	}
}

static void
kw_intr(Ureg*, void *arg)
{
	Uart *uart = arg;
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	ulong v;

	v = regs->iir;
	if(v&IRRthrempty)
		uartkick(uart);
	if(v&IRRrxdata)
		kw_read(uart);

	intrclear(Irqhi, ctlr->irq);
}

static Uart*
kw_pnp(void)
{
	return kirkwooduart;
}

static void
kw_enable(Uart* uart, int ie)
{
        Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;

	regs->fcr = FCRenable|FCRrxtrigger4;
	regs->ier = IERrx|IERtx;
	intrenable(Irqhi, ctlr->irq, kw_intr, uart, uart->name);

        (*uart->phys->dtr)(uart, 1);
        (*uart->phys->rts)(uart, 1);
}

static void
kw_disable(Uart* uart)
{
        Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;

        (*uart->phys->dtr)(uart, 0);
        (*uart->phys->rts)(uart, 0);
        (*uart->phys->fifo)(uart, 0);

	intrdisable(Irqhi, ctlr->irq, kw_intr, uart, uart->name);
}

static void
kw_kick(Uart* uart)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	int i;

	if(uart->cts == 0 || uart->blocked)
		return;

	for(i = 0; i < 16; i++) {
		if((regs->lsr&LSRthre) == 0)
			break;
		if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
			break;
		regs->thr = *uart->op++;
	}
}

static void
kw_break(Uart* uart, int ms)
{
	// xxx
}

static int
kw_baud(Uart* uart, int baud)
{
	// xxx
	return 0;
}

static int
kw_bits(Uart* uart, int bits)
{
	// xxx
	return 0;
}

static int
kw_stop(Uart* uart, int stop)
{
	// xxx
	return 0;
}

static int
kw_parity(Uart* uart, int parity)
{
	// xxx
	return 0;
}

static void
kw_modemctl(Uart* uart, int on)
{
	// xxx
}

static void
kw_rts(Uart* uart, int on)
{
	// xxx
}

static void
kw_dtr(Uart* uart, int on)
{
	// xxx
}

static long
kw_status(Uart* uart, void* buf, long n, long offset)
{
	// xxx
	return 0;
}

static void
kw_fifo(Uart* uart, int level)
{
	// xxx
}

static int
kw_getc(Uart *uart)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;

	while((regs->lsr&LSRrx) == 0)
		delay();
	return regs->rbr;
}

static void
kw_putc(Uart *uart, int c)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;

	while((regs->lsr&LSRthre) == 0)
		delay();
	regs->thr = c;
}

PhysUart kirkwoodphysuart = { 
	.name		= "kirkwood",
	.pnp		= kw_pnp,
	.enable		= kw_enable,
	.disable	= kw_disable,
	.kick		= kw_kick,
	.dobreak	= kw_break,
	.baud		= kw_baud,
	.bits		= kw_bits,
	.stop		= kw_stop,
	.parity		= kw_parity,
	.modemctl	= kw_modemctl,
	.rts		= kw_rts,
	.dtr		= kw_dtr,
	.status		= kw_status,
	.fifo		= kw_fifo,
	.getc		= kw_getc,
	.putc		= kw_putc,
};


void
uartconsole(void)
{
	Uart *uart;

	uart = &kirkwooduart[0];
	consuart = uart;
	uart->console = 1;
}


static void
delay(void)
{
	int i;
	for(i = 0; i < 1024; i++)
		;
}

void
serialputc(int c)
{
	while((UART0REG->lsr&LSRthre) == 0)
		delay();
	UART0REG->thr = c;
}

void
serialputs(char *p, int len)
{
	while(--len >= 0) {
		if(*p == '\n')
			serialputc('\r');
		serialputc(*p++);
	}
}
