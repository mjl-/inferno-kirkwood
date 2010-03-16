#include "mem.h"

/*
 * what's the state when we get here, what u-boot set up?
 * i'm guessing:  no interrupts, no caches, no mmu
 */

TEXT _startup(SB), $-4
	MOVW	$setR12(SB), R12

	MOVW	$(MACHADDR+KSTACK-4), R13	/* leave 4 bytes for link */

	MOVW	$(PsrDirq|PsrDfiq|PsrMsvc), R1	/* ensure svc mode, no interrupts */
	MOVW	R1, CPSR

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
	MOVW	$0, R0
	MCR	CpMMU, 0, R0, C7, C10, 4		/* drain write buffer */
	MCR	CpMMU, 0, R0, C(CpCacheCtl), C0, 4	/* wait for interrupt */
	RET


TEXT cpuidget(SB), $-4
	MRC	CpMMU, 0, R0, C(CpCPUID), C0, 0
	RET

TEXT cacheget(SB), $-4
	MRC	CpMMU, 0, R0, C(CpCacheID), C0, 1
	RET

TEXT tcmget(SB), $-4
	MRC	CpMMU, 0, R0, C(CpCacheID), C0, 2
	RET

TEXT cpctlget(SB), $-4
	MRC	CpMMU, 0, R0, C(CpControl), C0, 0
	RET
 
TEXT cpctlput(SB), $-4
	MCR	CpMMU, 0, R0, C(CpControl), C0, 0
	RET

TEXT ttbget(SB), $-4
	MRC	CpMMU, 0, R0, C(CpTTB), C0, 0
	RET

TEXT ttbput(SB), $-4
	MCR	CpMMU, 0, R0, C(CpTTB), C0, 0
	RET

TEXT dacget(SB), $-4
	MRC	CpMMU, 0, R0, C(CpDAC), C0, 0
	RET
	
TEXT dacput(SB), $-4
	MCR	CpMMU, 0, R0, C(CpDAC), C0, 0
	RET

TEXT dclockdownget(SB), $-4
	MRC	CpMMU, 0, R0, C9, C0, 0
	RET

TEXT dclockdownput(SB), $-4
	MCR	CpMMU, 0, R0, C9, C0, 0
	RET

TEXT iclockdownget(SB), $-4
	MRC	CpMMU, 0, R0, C9, C0, 1
	RET

TEXT iclockdownput(SB), $-4
	MCR	CpMMU, 0, R0, C9, C0, 1
	RET

TEXT tlblockdownget(SB), $-4
	MRC	CpMMU, 0, R0, C(CpTLBLk), C0, 0
	RET

TEXT tlblockdownput(SB), $-4
	MCR	CpMMU, 0, R0, C(CpTLBLk), C0, 0
	RET

TEXT fcsepidget(SB), $-4
	MRC	CpMMU, 0, R0, C13, C0, 0
	RET

TEXT fcsepidput(SB), $-4
	MCR	CpMMU, 0, R0, C13, C0, 0
	RET

TEXT contextidget(SB), $-4
	MRC	CpMMU, 0, R0, C13, C0, 1
	RET

TEXT tlbclear(SB), $-4
	MCR	CpMMU, 0, R0, C(CpTLBops), C7, 0


/*
"write back" (including draining write buffer) and invalidating.
*/

TEXT icinvall(SB), $-4
icinvall0:
	MOVW	$0, R0
	MCR	CpMMU, 0, R0, C7, C5, 0
	RET

TEXT icinv(SB), $-4
	MOVW	4(FP), R1
	CMP	$(CACHESIZE/2), R1
	BGE	icinvall0
	ADD	R0, R1
	BIC	$(CACHELINESIZE-1), R0
icinv0:
	MCR	CpMMU, 0, R0, C7, C5, 1
	ADD	$CACHELINESIZE, R0
	CMP	R1, R0
	BLO	icinv0
	RET


#define DRAINWB		MOVW	$0, R2; \
			MCR	CpMMU, 0, R2, C7, C10, 4

/* arm926ej-s' special test,clean,invalidate instruction does not seem to work.  walk through each way for each set. */
TEXT dcwball(SB), $-4
dcwball0:
	MOVW	$(127<<5), R1			/* start at set 128 */
wbset:
	ORR	$(3<<30), R1, R0		/* start at way 4 */
wbway:
	MCR	CpMMU, 0, R0, C7, C10, 2	/* clean set/way */
	SUB.S	$(1<<30), R0			/* flag C for no borrow: another way */
	BCS	wbway
	SUB.S	$(1<<5), R1			/* flag C for no borrow: another set */
	BCS	wbset
	DRAINWB
	RET

TEXT dcwb(SB), $-4
	MOVW	4(FP), R1
	CMP	$(CACHESIZE), R1
	BCS	dcwball0
	ADD	R0, R1
	BIC	$(CACHELINESIZE-1), R0
dcwb0:
	MCR	CpMMU, 0, R0, C7, C10, 1
	ADD	$CACHELINESIZE, R0
	CMP	R1, R0
	BLO	dcwb0
	DRAINWB
	RET

TEXT dcwbinvall(SB), $-4
dcwbinvall0:
	MOVW	$(127<<5), R1			/* start at set 128 */
wbinvset:
	ORR	$(3<<30), R1, R0		/* start at way 4 */
wbinvway:
	MCR	CpMMU, 0, R0, C7, C14, 2	/* clean & invalidate set/way */
	SUB.S	$(1<<30), R0			/* flag C for no borrow: another way */
	BCS	wbinvway
	SUB.S	$(1<<5), R1			/* flag C for no borrow: another set */
	BCS	wbinvset
	DRAINWB
	RET

TEXT dcwbinv(SB), $-4
	MOVW	4(FP), R1
	CMP	$(CACHESIZE), R1
	BCS	dcwbinvall0
	ADD	R0, R1
	BIC	$(CACHELINESIZE-1), R0
dcwbinv0:
	MCR	CpMMU, 0, R0, C7, C14, 1
	ADD	$CACHELINESIZE, R0
	CMP	R1, R0
	BLO	dcwbinv0
	DRAINWB
	RET

TEXT dcinvall(SB), $-4
	MOVW	$0, R0
	MCR	CpMMU, 0, R0, C7, C6, 0
	RET

TEXT dcinv(SB), $-4
	MOVW	4(FP), R1
	ADD	R0, R1
	BIC	$(CACHELINESIZE-1), R0
dcinv0:
	MCR	CpMMU, 0, R0, C7, C6, 1
	ADD	$CACHELINESIZE, R0
	CMP	R1, R0
	BLO	dcinv0
	RET

TEXT mvfeatget(SB), $-4
	MRC	CpMMU, 1, R0, C15, C1, 0
	RET

TEXT mvfeatset(SB), $-4
	MCR	CpMMU, 1, R0, C15, C1, 0
	RET
