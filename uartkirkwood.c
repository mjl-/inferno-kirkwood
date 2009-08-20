#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/uart.h"

enum {
	UartFREQ =	CLOCKFREQ,
};

extern PhysUart kirkwoodphysuart;

typedef struct Ctlr Ctlr;
struct Ctlr {
	UartReg*	regs;
	int	irq;
	int	iena;
	int	fena;
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

	/*
 	 * Enable interrupts and turn on DTR and RTS.
	 * Be careful if this is called to set up a polled serial line
	 * early on not to try to enable interrupts as interrupt-
	 * -enabling mechanisms might not be set up yet.
	 */
	if(ie){
		if(ctlr->iena == 0){
			(*uart->phys->fifo)(uart, 4);
			regs->ier = IERrx|IERtx;
			intrenable(Irqhi, ctlr->irq, kw_intr, uart, uart->name);

			ctlr->iena = 1;
		}
	}

	(*uart->phys->dtr)(uart, 1);
	(*uart->phys->rts)(uart, 1);
}

static void
kw_disable(Uart* uart)
{
	Ctlr *ctlr = uart->regs;

	(*uart->phys->dtr)(uart, 0);
	(*uart->phys->rts)(uart, 0);
	(*uart->phys->fifo)(uart, 0);

	if(ctlr->iena != 0){
		if(intrdisable(Irqhi, ctlr->irq, kw_intr, uart, uart->name) == 0)
			ctlr->iena = 0;
	}
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
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	
	/*
	 * Send a break.
	 */
	if(ms <= 0)
		ms = 200;

	ctlr = uart->regs;
	regs->lcr |= LCRbreak;
	tsleep(&up->sleep, return0, 0, ms);
	regs->lcr &= ~LCRbreak;
}

static int
kw_baud(Uart* uart, int baud)
{
	ulong bgc;
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	
	/*
	 * Set the Baud rate by calculating and setting the Baud rate
	 * Generator Constant. This will work with fairly non-standard
	 * Baud rates.
	 */
	if(uart->freq == 0 || baud <= 0)
		return -1;
	bgc = (uart->freq+8*baud-1)/(16*baud);

	regs->lcr |= LCRdivlatch;
	regs->dll = bgc & 0xff;
	regs->dlh = (bgc>>8) & 0xff;
	regs->lcr &= ~LCRdivlatch;

	uart->baud = baud;

	return 0;
}

static int
kw_bits(Uart* uart, int bits)
{
	int lcr;
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	
	lcr = regs->lcr & ~LCRbpcmask;

	switch(bits){
	case 5:
		lcr |= LCRbpc5;
		break;
	case 6:
		lcr |= LCRbpc6;
		break;
	case 7:
		lcr |= LCRbpc7;
		break;
	case 8:
		lcr |= LCRbpc8;
		break;
	default:
		return -1;
	}
	regs->mcr = lcr;

	uart->bits = bits;

	return 0;
}

static int
kw_stop(Uart* uart, int stop)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	int lcr;

	lcr = regs->lcr & ~LCRstop2b;

	switch(stop){
	case 1:
		break;
	case 2:
		lcr |= LCRstop2b;
		break;
	default:
		return -1;
	}
	regs->lcr = lcr;

	uart->stop = stop;
	return 0;
}

static int
kw_parity(Uart* uart, int parity)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	int lcr;

	lcr = regs->lcr & ~(LCRparity|LCRparityeven);

	switch(parity){
	case 'e':
		lcr |= LCRparity|LCRparityeven;
		break;
	case 'o':
		lcr |= LCRparity;
		break;
	case 'n':
		break;
	default:
		return -1;
	}

	regs->lcr = lcr;
	uart->parity = parity;

	return 0;
}

static void
kw_modemctl(Uart* uart, int on)
{
	USED(uart, on);
}

static void
kw_rts(Uart* uart, int on)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	ulong mcr;

	mcr = regs->mcr & ~MCRrts;
	
	/*
	 * Toggle RTS.
	 */
	if(on)
		mcr |= MCRrts;
	else
		mcr &= ~MCRrts;
	regs->mcr = mcr;
}

static void
kw_dtr(Uart* uart, int on)
{
	USED(uart, on);
}

static long
kw_status(Uart* uart, void* buf, long n, long offset)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;
	uchar ier, lcr, mcr, msr;
	char *p;

	p = malloc(READSTR);
	mcr = regs->mcr;
	msr = regs->msr;
	ier = regs->ier;
	lcr = regs->lcr;
	snprint(p, READSTR,
		"b%d c%d e%d l%d m%d p%c r%d s%d i%d\n"
		"dev(%d) type(%d) framing(%d) overruns(%d) "
		"berr(%d) serr(%d)%s\n",

		uart->baud,
		uart->hup_dcd, 
		uart->hup_dsr,
		(lcr & LCRbpcmask) + 5,
		(ier & IERems) != 0, 
		(lcr & LCRparity) ? ((lcr & LCRparityeven) ? 'e': 'o'): 'n',
		(mcr & MCRrts) != 0,
		(lcr & LCRstop2b) ? 2: 1,
		ctlr->fena,

		uart->dev,
		uart->type,
		uart->ferr,
		uart->oerr,
		uart->berr,
		uart->serr,
		(msr & MSRdcts) ? " cts": ""
	);
	n = readstr(offset, buf, n, p);
	free(p);

	return n;
}

static void
kw_fifo(Uart* uart, int level)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;

	/*
	 * Set the trigger level, default is the max value.
	 */
	ilock(ctlr);
	ctlr->fena = level;
	switch(level){
	case 0:
		break;
	case 1:
		level = FCRrxtrigger1|FCRenable;
		break;
	case 4:
		level = FCRrxtrigger4|FCRenable;
		break;
	case 8:
		level = FCRrxtrigger8|FCRenable;
		break;
	default:
		level = FCRrxtrigger14|FCRenable;
		break;
	}
	regs->fcr = level;
	iunlock(ctlr);
}

static int
kw_getc(Uart *uart)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;

	while((regs->lsr&LSRrx) == 0)
		delay(1);
	return regs->rbr;
}

static void
kw_putc(Uart *uart, int c)
{
	Ctlr *ctlr = uart->regs;
	UartReg *regs = ctlr->regs;

	while((regs->lsr&LSRthre) == 0)
		delay(1);
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
	int n;
	char *cmd, *p;

//	if((p = getconf("console")) == nil){
//		return;
//	n = strtoul(p, &cmd, 0);
//	if(p == cmd)
//		return;

	switch(n){
	default:
		return;
	case 0:
		uart = &kirkwooduart[0];
		break;
	}

	(*uart->phys->enable)(uart, 0);
	uartctl(uart, "b115200 l8 pn s1");
	if(cmd && *cmd != '\0')
		uartctl(uart, cmd);

	consuart = uart;
	uart->console = 1;
}


void
serialputc(int c)
{
	while((UART0REG->lsr&LSRthre) == 0)
		microdelay(100);
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
