/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

TRAPHANDLER_NOEC(divide_handler,T_DIVIDE)          //   0	  // divide error
TRAPHANDLER_NOEC(debug_handler,T_DEBUG)            //   1	  // debug exception
TRAPHANDLER_NOEC(nmi_handler,T_NMI)                //   2	  // non-maskable interrupt
TRAPHANDLER_NOEC(brkpt_handler,T_BRKPT)            //   3	  // breakpoint
TRAPHANDLER_NOEC(oflow_handler,T_OFLOW)            //   4	  // overflow
TRAPHANDLER_NOEC(bound_handler,T_BOUND)            //   5      // bounds check
TRAPHANDLER_NOEC(illop_handler,T_ILLOP)            //   6      // illegal opcode
TRAPHANDLER_NOEC(device_handler,T_DEVICE)          //   7      // device not available

TRAPHANDLER(dblflt_handler,T_DBLFLT)               //   8      // double fault
TRAPHANDLER(tss_handler,T_TSS)                     //  10      // invalid task switch segment
TRAPHANDLER(segnp_handler,T_SEGNP)                 //  11      // segment not present
TRAPHANDLER(stack_handler,T_STACK)                 //  12      // stack exception
TRAPHANDLER(gpflt_handler,T_GPFLT)                 //  13      // general protection fault
TRAPHANDLER(pgflt_handler,T_PGFLT)                 //  14      // page fault

TRAPHANDLER_NOEC(fperr_handler,T_FPERR)            //  16      // floating point error

TRAPHANDLER(align_handler,T_ALIGN)            	   //  17	  // aligment check

TRAPHANDLER_NOEC(mchk_handler,T_MCHK)              //  18	  // machine check
TRAPHANDLER_NOEC(simderr_handler,T_SIMDERR)        //  19	  // SIMD floating point error

TRAPHANDLER_NOEC(syscall_handler,T_SYSCALL)        //  48	  // System Call

TRAPHANDLER_NOEC(timer_handler,IRQ_OFFSET + IRQ_TIMER) // 0
TRAPHANDLER_NOEC(kbd_handler,IRQ_OFFSET + IRQ_KBD)
TRAPHANDLER_NOEC(e1000_handler,IRQ_OFFSET + IRQ_E1000)
TRAPHANDLER_NOEC(hw_handler_2,IRQ_OFFSET + 2)
TRAPHANDLER_NOEC(hw_handler_3,IRQ_OFFSET + 3)
TRAPHANDLER_NOEC(serial_handler,IRQ_OFFSET + IRQ_SERIAL)
TRAPHANDLER_NOEC(hw_handler_5,IRQ_OFFSET + 5)
TRAPHANDLER_NOEC(hw_handler_6,IRQ_OFFSET + 6)
TRAPHANDLER_NOEC(spurious_handler,IRQ_OFFSET + IRQ_SPURIOUS) // 7
TRAPHANDLER_NOEC(hw_handler_8,IRQ_OFFSET + 8)
TRAPHANDLER_NOEC(hw_handler_9,IRQ_OFFSET + 9)
TRAPHANDLER_NOEC(hw_handler_10,IRQ_OFFSET + 10)
TRAPHANDLER_NOEC(hw_handler_11,IRQ_OFFSET + 11)
TRAPHANDLER_NOEC(hw_handler_12,IRQ_OFFSET + 12)
TRAPHANDLER_NOEC(hw_handler_13,IRQ_OFFSET + 13)
TRAPHANDLER_NOEC(hw_handler_14,IRQ_OFFSET + 14)
TRAPHANDLER_NOEC(hw_handler_15,IRQ_OFFSET + 15)

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */



/*
 * Lab 3: Your code here for _alltraps
 */

 _alltraps:
  # Build trap frame.
  pushl %ds
  pushl %es
  pushal

  # Set up data and per-cpu segments.
  movw $(GD_KD), %ax
  movw %ax, %ds
  movw %ax, %es

  # Call trap(tf), where tf=%esp
  pushl %esp
  call trap
