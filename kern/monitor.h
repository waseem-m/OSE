#ifndef JOS_KERN_MONITOR_H
#define JOS_KERN_MONITOR_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

struct Trapframe;

// Activate the kernel monitor,
// optionally providing a trap frame indicating the current state
// (NULL if none).
void monitor(struct Trapframe *tf);

// Functions implementing monitor commands.
int mon_continue_execution(int argc, char **argv, struct Trapframe *tf);
int mon_step(int argc, char **argv, struct Trapframe *tf);
int mon_help(int argc, char **argv, struct Trapframe *tf);
int mon_kerninfo(int argc, char **argv, struct Trapframe *tf);
int mon_backtrace(int argc, char **argv, struct Trapframe *tf);
int mon_showmapping(int argc, char **argv, struct Trapframe *tf);
int mon_permmappings(int argc, char **argv, struct Trapframe *tf);
int mon_dumpmem(int argc, char **argv, struct Trapframe *tf);
void mon_va_info(uintptr_t va);

#endif	// !JOS_KERN_MONITOR_H
