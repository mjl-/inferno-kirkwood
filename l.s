#include "mem.h"

/*
 * what's the state when we get here, what u-boot set up?
 * i'm guessing:  no interrupts, no caches
 */

TEXT _startup(SB), $-4
	MOVW	$setR12(SB), R12

	MOVW	$(PsrDirq|PsrDfiq|PsrMsvc), R1	/* ensure svc mode, no interrupts */
	MOVW	R1, CPSR

	MOVW	$(MACHADDR+KSTACK-4), R13

	BL	main(SB)	/* jump to kernel */
dead:
	B	dead
	BL	_div(SB)	/* hack to get _div etc loaded */

/* set stack pointer for given mode */
TEXT setr13(SB), $-4
	MOVW		4(FP), R1

	MOVW		CPSR, R2
	BIC		$PsrMask, R2, R3
	ORR		R0, R3
	MOVW		R3, CPSR

	MOVW		R13, R0
	MOVW		R1, R13

	MOVW		R2, CPSR
	RET

TEXT vectors(SB), $-4
	MOVW	0x18(R15), R15			/* reset */
	MOVW	0x18(R15), R15			/* undefined */
	MOVW	0x18(R15), R15			/* SWI */
	MOVW	0x18(R15), R15			/* prefetch abort */
	MOVW	0x18(R15), R15			/* data abort */
	MOVW	0x18(R15), R15			/* reserved */
	MOVW	0x18(R15), R15			/* IRQ */
	MOVW	0x18(R15), R15			/* FIQ */

TEXT vtable(SB), $-4
	WORD	$_vsvc(SB)			/* reset, in svc mode already */
	WORD	$_vund(SB)			/* undefined, switch to svc mode */
	WORD	$_vsvc(SB)			/* swi, in svc mode already */
	WORD	$_vpab(SB)			/* prefetch abort, switch to svc mode */
	WORD	$_vdab(SB)			/* data abort, switch to svc mode */
	WORD	$_vsvc(SB)			/* reserved */
	WORD	$_virq(SB)			/* IRQ, switch to svc mode */
	WORD	$_vfiq(SB)			/* FIQ, switch to svc mode */

TEXT _vund(SB), $-4
	MOVM.DB		[R0-R3], (R13)
	MOVW		$PsrMund, R0
	B		_vswitch

TEXT _vpab(SB), $-4
	MOVM.DB		[R0-R3], (R13)
	MOVW		$PsrMabt, R0
	B		_vswitch

TEXT _vdab(SB), $-4
	MOVM.DB		[R0-R3], (R13)
	MOVW		$(PsrMabt+1), R0
	B		_vswitch

TEXT _vfiq(SB), $-4				/* FIQ */
	MOVM.DB		[R0-R3], (R13)
	MOVW		$PsrMfiq, R0
	B		_vswitch

TEXT _vsvc(SB), $-4
	MOVW.W		R14, -4(R13)
	MOVW		CPSR, R14
	MOVW.W		R14, -4(R13)
	BIC		$PsrMask, R14
	ORR		$(PsrDirq|PsrDfiq|PsrMsvc), R14
	MOVW		R14, CPSR
	MOVW		$PsrMsvc, R14
	MOVW.W		R14, -4(R13)
	B		_vsaveu

TEXT _virq(SB), $-4				/* IRQ */
	MOVM.DB		[R0-R3], (R13)
	MOVW		$PsrMirq, R0

_vswitch:					/* switch to svc mode */
	MOVW		SPSR, R1
	MOVW		R14, R2
	MOVW		R13, R3

	MOVW		CPSR, R14
	BIC		$PsrMask, R14
	ORR		$(PsrDirq|PsrDfiq|PsrMsvc), R14
	MOVW		R14, CPSR

	MOVM.DB.W	[R0-R2], (R13)
	MOVM.DB		(R3), [R0-R3]

_vsaveu:						/* Save Registers */
	MOVW.W		R14, -4(R13)			/* save link */

	SUB		$8, R13
	MOVM.DB.W	[R0-R12], (R13)
	MOVW		R0, R0				/* gratuitous noop */

	MOVW		$setR12(SB), R12		/* static base (SB) */
	MOVW		R13, R0				/* argument is ureg */
	SUB		$8, R13				/* space for arg+lnk*/
	BL		trap(SB)

_vrfe:							/* Restore Regs */
	MOVW		CPSR, R0			/* splhi on return */
	ORR		$(PsrDirq|PsrDfiq), R0, R1
	MOVW		R1, CPSR
	ADD		$(8+4*15), R13		/* [r0-R14]+argument+link */
	MOVW		(R13), R14			/* restore link */
	MOVW		8(R13), R0
	MOVW		R0, SPSR
	MOVM.DB.S	(R13), [R0-R14]		/* restore user registers */
	MOVW		R0, R0				/* gratuitous nop */
	ADD		$12, R13		/* skip saved link+type+SPSR*/
	RFE					/* MOVM.IA.S.W (R13), [R15] */


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

TEXT cpsrr(SB), $-4
	MOVW	CPSR, R0
	RET

TEXT spsrr(SB), $-4
	MOVW	SPSR, R0
	RET

TEXT gotopc(SB), $-4
	MOVW	R0, R1
	MOVW	$0, R0
	MOVW	R1, PC
	RET

TEXT idle(SB), $-4
	MCR	CpMMU, 0, R0, C(CpCacheCtl), C(0), 4
	RET

TEXT getcpuid(SB), $-4
	MRC	CpMMU, 0, R0, C(CpCPUID), C(0)
	RET

TEXT tcmstat(SB), $-4
	MRC	CpMMU, 0, R0, C(CpCPUID), C(0), 2
	RET

TEXT rcpctl(SB), $-4
	MRC	CpMMU, 0, R0, C(CpControl), C(0), 0
	RET
 
TEXT wcpctl(SB), $-4
	MCR	CpMMU, 0, R0, C(CpControl), C(0), 0
	RET

TEXT mmuinit(SB), $-4
	/* enable icache, dcache and mpu */
	MRC	CpMMU, 0, R0, C(CpControl), C0, 0
	ORR	$(CpCrrob|CpCIcache|CpCDcache), R0
	BIC	$(CpCDcache), R0	/* no dcache for now, will have to flush in drivers doing dma first. */
	MCR	CpMMU, 0, R0, C(CpControl), C0, 0
	RET


TEXT icflushall(SB), $-4
	MCR	CpMMU, 0, R1, C(CpCacheCtl), C5, 0 
	RET

TEXT dcflushall(SB), $-4
	MCR	CpMMU, 0, R1, C(CpCacheCtl), C6, 0
	RET
