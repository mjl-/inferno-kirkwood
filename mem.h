#define BI2BY		8			/* bits per byte */
#define BI2WD		32			/* bits per word */
#define BY2WD		4			/* bytes per word */
#define BY2V		8			/* bytes per double word */
#define BY2PG		4096			/* bytes per page */
#define WD2PG		(BY2PG/BY2WD)		/* words per page */
#define PGSHIFT		12			/* log(BY2PG) */
#define ROUND(s, sz)	(((s)+(sz-1))&~(sz-1))
#define PGROUND(s)	ROUND(s, BY2PG)
#define BIT(n)		(1<<n)
#define BITS(a,b)	((1<<(b+1))-(1<<a))

#define MAXMACH		1			/* max # cpus system can run */

/*
 * Time
 */
#define HZ		(100)			/* clock frequency */
#define MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define TK2SEC(t)	((t)/HZ)		/* ticks to seconds */
#define MS2TK(t)	((t)/MS2HZ)		/* milliseconds to ticks */

/*
 * More accurate time
 */
#define CLOCKFREQ	200000000	/* 200 mhz */
#define MS2TMR(t)	((ulong)(((uvlong)(t)*CLOCKFREQ)/1000))
#define US2TMR(t)	((ulong)(((uvlong)(t)*CLOCKFREQ)/1000000))


#define KZERO		0x0000000
#define KTZERO		(KZERO+0x8000)
#define MACHADDR	(KZERO+0x1000)

#define KSTACK	8192		/* Size of kernel stack */


/*
 * PSR
 */
#define PsrMusr		0x10	/* mode */
#define PsrMfiq		0x11 
#define PsrMirq		0x12
#define PsrMsvc		0x13
#define PsrMabt		0x17
#define PsrMund		0x1B
#define PsrMsys		0x1F
#define PsrMask		0x1F

#define PsrDfiq		0x00000040	/* disable FIQ interrupts */
#define PsrDirq		0x00000080	/* disable IRQ interrupts */

#define PsrV		0x10000000	/* overflow */
#define PsrC		0x20000000	/* carry/borrow/extend */
#define PsrZ		0x40000000	/* zero */
#define PsrN		0x80000000	/* negative/less than */
