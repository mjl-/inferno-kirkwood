#include "mem.h"

/*
 * what's the state when we get here?  i.e. what does u-boot set up for us?
 * i'm guessing:  no interrupts, no caches
 */

TEXT _startup(SB), $-4
	MOVW	$setR12(SB), R12
	MOVW	$(MACHADDR+KSTACK-4), R13
	/* xxx MOVW	R1, CPSR */
	BL	main(SB)
dead:
	B	dead
	BL	_div(SB)	/* hack to get _div etc loaded */


TEXT getcallerpc(SB), $-4
	MOVW	0(R13), R0
	RET

TEXT _tas(SB), $-4
	MOVW	R0, R1
	MOVW	$0xDEADDEAD, R2
	SWPW	R2, (R1), R0
	RET

TEXT setlabel(SB), $-4
	MOVW	R13, 0(R0)		/* sp */
	MOVW	R14, 4(R0)		/* pc */
	MOVW	$0, R0
	RET

TEXT gotolabel(SB), $-4
	MOVW	0(R0), R13		/* sp */
	MOVW	4(R0), R14		/* pc */
	MOVW	$1, R0
	RET


/* xxx have to fill this in */
#define PsrDirq 1
#define PsrDfiq 2
TEXT splhi(SB), $-4
	MOVW	CPSR, R0
	ORR	$(PsrDirq), R0, R1
	MOVW	R1, CPSR
	MOVW	$(MACHADDR), R6
	MOVW	R14, (R6)	/* m->splpc */
	RET

TEXT spllo(SB), $-4
	MOVW	CPSR, R0
	BIC	$(PsrDirq|PsrDfiq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT splx(SB), $-4
	MOVW	$(MACHADDR), R6
	MOVW	R14, (R6)	/* m->splpc */

TEXT splxpc(SB), $-4
	MOVW	R0, R1
	MOVW	CPSR, R0
	MOVW	R1, CPSR
	RET

TEXT spldone(SB), $-4
	RET

TEXT islo(SB), $-4
	MOVW	CPSR, R0
	AND	$(PsrDirq), R0
	EOR	$(PsrDirq), R0
	RET

TEXT splfhi(SB), $-4
	MOVW	CPSR, R0
	ORR	$(PsrDfiq|PsrDirq), R0, R1
	MOVW	R1, CPSR
	RET

TEXT splflo(SB), $-4
	MOVW	CPSR, R0
	BIC	$(PsrDfiq), R0, R1
	MOVW	R1, CPSR
	RET
