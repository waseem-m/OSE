// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display call stack", mon_backtrace },
	{ "showmappings", "Show mapping in range. Format: [showmapping <address_start> <address_end>]", mon_showmapping},
	{ "permmappings", "Change permissions. Format: [permmappings <va> <set/clear/flip> <u/w>]", mon_permmappings},
	{ "dumpmem", "dump memory content. Format: [dumpmem <start_address> <end_address> <v/p>]", mon_dumpmem},
	{ "continue", "continue running current environment without breaking", mon_continue_execution},
	{ "step", "step one instruction in current environment", mon_step},
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/


int
mon_continue_execution(int argc, char **argv, struct Trapframe *tf){
    if (tf != NULL){
        env_pop_tf(tf);
    }
    panic("mon_continue: should not reach here");
    return 0;
}

int
mon_step(int argc, char **argv, struct Trapframe *tf){
    if (tf != NULL){
        tf->tf_eflags |= FL_TF;
        env_pop_tf(tf);
    }
    panic("mon_step: should not reach here");
    return 0;
}

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
    int ebp = read_ebp();

    while (ebp != 0){

        int* ebp_as_array = (int*) ebp;
        int eip = ebp_as_array[1];
        cprintf("ebp %08x eip %08x args", ebp, eip);
        int i;
        for (i = 2; i < 7; i++)
            cprintf(" %08x", ebp_as_array[i]);
        cprintf("\n");

        mon_va_info(eip);

        ebp = ebp_as_array[0];

    }
    cprintf("\n");

	return 0;
}

void mon_va_info(uintptr_t va){
    struct Eipdebuginfo info;
    debuginfo_eip(va , &info);
    int name_length = strfind(info.eip_fn_name,':') - info.eip_fn_name;
    cprintf("       %s:%d: %.*s+%d\n", info.eip_file,
                                       info.eip_line,
                                       name_length,
                                       info.eip_fn_name,
                                       (int) va - (int) info.eip_fn_addr);
}

void print_va_mapping(uint32_t va, physaddr_t pa, const char* flags){
    cprintf ("\nVA:0x%08x PA:0x%08x %s", va, pa, flags);
}

void print_va_has_no_mapping(uint32_t va){
    cprintf ("\nVA:0x%08x PA: XXXXX", va);
}

#define perm_set    1 << 0
#define perm_clear  1 << 1
#define perm_flip   1 << 2

int
mon_permmappings(int argc, char **argv, struct Trapframe *tf){

    int flags = 0;

    if (argc < 3) {
        cprintf("permmappings incorrect number of args. Correct format\n: [permmappings <address> <set/clear/flip> <u/w>]\n");
        return 0;
    }

    char* endptr;
    uint32_t va = ROUNDOWN_PGSIZE((uint32_t) strtol(argv[1], &endptr, 0));
    if (endptr != NULL && *endptr){
        cprintf("couldn't parse arg1: %s\n", argv[1]);
        return 0;
    }

    if (strcmp("set",argv[2]) == 0){
        flags |= perm_set;
    }

    else if (strcmp("clear",argv[2]) == 0){
        flags |= perm_clear;
    }

    else if (strcmp("flip",argv[2]) == 0){
        flags |= perm_flip;

    } else {

        cprintf("couldn't parse arg2: %s\n", argv[2]);
        return 0;
    }

    int mask = 0;
    if (strcmp("u",argv[3]) == 0){
        mask = PTE_U;
    }

    else if (strcmp("w",argv[3]) == 0){
        mask = PTE_W;
    }

    else {
        cprintf("couldn't parse arg3: %s\n", argv[3]);
        return 0;
    }

    pde_t* pgdir = (pde_t*) KADDR( (physaddr_t) rcr3());
    pte_t* pte = pgdir_walk(pgdir,(void *)(va),0);
    if (!pte || ((*pte & PTE_P) == 0)){
        cprintf("Address %p is not mapped\n", (void*) va);
        return 0;
    }

    if (flags & perm_set){
        *pte |= mask;
    }
    if (flags & perm_clear){
        *pte &= ~mask;
    }

    if (flags & perm_flip){
        *pte ^= mask;
    }

    return 0;
}

int
mon_showmapping(int argc, char **argv, struct Trapframe *tf){

    if (argc < 3) {
        cprintf("showmapping incorrect args. Correct format\n: showmapping <address_start> <address_end>\n");
        return 0;
    }

    char * endptr;
    uint32_t va = ROUNDOWN_PGSIZE((uint32_t) strtol(argv[1], &endptr, 0));
    if (endptr != NULL && *endptr){
        cprintf("couldn't parse arg1: %s\n", argv[1]);
        return 0;
    }
    uint32_t end_address = ROUNDOWN_PGSIZE((uint32_t) strtol(argv[2], &endptr, 0)) + (PGSIZE - 1);
    if (endptr != NULL && *endptr){
        cprintf("couldn't parse arg2: %s\n", argv[2]);
        return 0;
    }

    pde_t* pgdir = (pde_t*) KADDR( (physaddr_t) rcr3());

    cprintf("\n======");
    cprintf("\nmon_showmapping");
    cprintf("\n======");

    while (va < end_address){

        char flags_str[] = "RS";

        pte_t* current_pte = pgdir_walk(pgdir,(void *)(va),0);

        if (!current_pte || ((*current_pte & PTE_P) == 0)){
            print_va_has_no_mapping(va);
            goto end;
        }

        if (*current_pte & PTE_W){
            flags_str[0] = 'W';
        }

        if (*current_pte & PTE_U){
            flags_str[1] = 'U';
        }

        print_va_mapping(va, PTE_ADDR(*current_pte), flags_str);
end:
        va += PGSIZE;

        // Check overfloww
        if (va < PGSIZE){
            break;
        }

    }

    cprintf("\n\n");

    return 0;
}

void dump(uint32_t start, uint32_t end, bool isVirtual){
    uint32_t count = 0;
    cprintf("\n\n===========");
    cprintf("\nSTART OF DUMP");
    cprintf("\n===========\n");
    uint32_t value;

    uint32_t size = end - start + 1;
    uint32_t size_leftover = size - ROUNDDOWN(size,4);
    size -= size_leftover;

    while (size > 0 ){
        if (count++ % 4 == 0 ){
            cprintf("\n 0x%08x: ", start);
        }
        memcpy(&value, isVirtual ? (void*) start :  KADDR(start), 4);
        cprintf(" 0x%08x ", value);
        start += 4;
        size -= 4;
    }

    if (size_leftover){
        if (count++ % 4 == 0 ){
            cprintf("\n 0x%08x: ", start);
        }

        cprintf(" 0x");
        while (end >= start){
            uint32_t val = *(uint8_t*) (isVirtual ? (void*) end :  KADDR(end));
            cprintf("%02x",val);
            if (end == 0){
                break;
            }
            end--;
        }
    }

    cprintf("\n\n\n===========");
    cprintf("\nEND OF DUMP");
    cprintf("\n===========\n\n");

}

int
mon_dumpmem(int argc, char **argv, struct Trapframe *tf){

    if (argc < 4) {
        cprintf("dumpmem incorrect args. Correct format\n: dumpmem <start_address> <end_address> <v/p>\n");
        return 0;
    }

    char * endptr;
    uint32_t address_start = (uint32_t) strtol(argv[1], &endptr, 0);
    if (endptr != NULL && *endptr){
        cprintf("couldn't parse arg1: %s\n", argv[1]);
        return 0;
    }
    uint32_t address_end = (uint32_t) strtol(argv[2], &endptr, 0);
    if (endptr != NULL && *endptr){
        cprintf("couldn't parse arg2: %s\n", argv[2]);
        return 0;
    }

    if (address_end < address_start){
        cprintf("end address small than start address\n");
        return 0;
    }

    bool isVirtual = false;
    if (strcmp("v",argv[3]) == 0){
        isVirtual = true;
    } else if (strcmp("p",argv[3]) == 0){
        isVirtual = false;
    } else {
        cprintf("couldn't parse arg3: %s\n", argv[3]);
        return 0;
    }

    dump(address_start,address_end, isVirtual);

    return 0;
}


/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
