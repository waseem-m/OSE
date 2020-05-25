// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

extern void _pgfault_upcall();

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{


	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	if (!(err & FEC_WR)){
	    panic ("fork : pgfault : read access");
	}

	// LAB 4: Your code here.
	pte_t pte = uvpt[(uintptr_t)addr >> PGSHIFT];
	if (!(pte & (PTE_COW))){
	    panic ("fork : pgfault : Actual permissions are: 0x%x", PTE_FLAGS(pte));
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	int result;
	if ((result = sys_page_alloc(0, (void*) PFTEMP, PTE_W )) < 0){
	    panic ("fork : pgfault : sys_page_alloc failed: %e", result);
	}

	// move data from addr too PFTEMP
	void* round_address = ROUNDDOWN(addr, PGSIZE);
	memmove(PFTEMP, round_address, PGSIZE );

	// Allocate address (TODO: delete previous content? what if more than one reference
    if ((result = sys_page_map(0, PFTEMP, 0, round_address,  PTE_W )) < 0){
        panic ("fork pgfault: pgfault : sys_page_alloc failed: %e", result);
    }

	sys_page_unmap(thisenv->env_id, PFTEMP);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int result;

	if ((pn << 12) == UXSTACKTOP - PGSIZE) {
	    panic("\nDEBUG\n");
	}

	pde_t pde = uvpd[pn >> 10];
	pte_t pte = uvpt[pn];
	void* address = (void*) (pn * PGSIZE);

	if ((pde & pte & PTE_P) == 0){
	    panic("fork : duppage : page doesn't exists:  %p ", (uintptr_t) address << PTXSHIFT);
	}

	pte_t flags = pte & PTE_SYSCALL;
	bool copy_on_write = (flags & (PTE_COW | PTE_W)) != 0;

	// Lab5:
	// If shared, copy pte (pte & PTE_SYSCALL) without change
	copy_on_write = copy_on_write && !(flags & PTE_SHARE);

	// Check if need to change to copy on write
	if (copy_on_write) {
	    flags &= ~(pte_t) PTE_W;
	    flags |= (pte_t) PTE_COW;
	}


	if ((result = sys_page_map(thisenv->env_id, address , envid, address , flags)) < 0){
	    panic("fork : duppage : page: %p , %e", (uintptr_t) address , result);
	}

	if (!copy_on_write){
	    return 0;
	}

    if ((result = sys_page_map(thisenv->env_id, address , thisenv->env_id, address , flags)) < 0){
        panic("fork : duppage : %e", result);
    }

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
    envid_t envid;
    uint8_t *addr;
    int result;

    set_pgfault_handler(pgfault);

    uintptr_t p = (uintptr_t) thisenv->env_pgfault_upcall;
    int x = 1;

    // Allocate a new child environment.
    // The kernel will initialize it with a copy of our register state,
    // so that the child will appear to have called sys_exofork() too -
    // except that in the child, this "fake" call to sys_exofork()
    // will return 0 instead of the envid of the child.
    envid = sys_exofork();
    if (envid < 0)
        panic("sys_exofork: %e", envid);
    if (envid == 0) {
        // We're the child.
        // The copied value of the global variable 'thisenv'
        // is no longer valid (it refers to the parent!).
        // Fix it and return 0.
        return 0;
    }

    // We're the parent.
    unsigned page;
    for (page = 0; page < PGNUM(UTOP); page += 1){

        if (page == PGNUM(UXSTACKTOP - PGSIZE)){
            continue;
        }

        pde_t pde = uvpd[page >> 10];
        if ((pde & PTE_P) == 0 ){
            continue;
        }
        pte_t pte = uvpt[page];
        if ((pte & PTE_P) == 0){
            continue;
        }
        duppage(envid, page);
    }

    sys_env_set_pgfault_upcall(envid,_pgfault_upcall);

    // Start the child environment running
    if ((result = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_W)) < 0){
        panic("fork : sys_page_alloc: %e", result);
    }

    if ((result = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
        panic("fork : sys_env_set_status: %e", result);


    return envid;
}

// Challenge!
int
sfork(void)
{
    envid_t envid;
    uint8_t *addr;
    int result;

    set_pgfault_handler(pgfault);

    // Allocate a new child environment.
    // The kernel will initialize it with a copy of our register state,
    // so that the child will appear to have called sys_exofork() too -
    // except that in the child, this "fake" call to sys_exofork()
    // will return 0 instead of the envid of the child.
    envid = sys_exofork();
    if (envid < 0)
        panic("sys_exofork: %e", envid);
    if (envid == 0) {
        // We're the child.
        // The copied value of the global variable 'thisenv'
        // is no longer valid (it refers to the parent!).
        // Fix it and return 0.
        return 0;
    }

    // We're the parent.
    unsigned page;
    for (page = 0; page < PGNUM(UTOP); page += 1){

        if (page == PGNUM(USTACKTOP - PGSIZE)){
            continue;
        }

        if (page == PGNUM(UXSTACKTOP - PGSIZE)){
            continue;
        }
        uintptr_t address = page << 12;

        pde_t pde = uvpd[page >> 10];
        if ((pde & PTE_P) == 0 ){
            continue;
        }
        pte_t pte = uvpt[page];
        if ((pte & PTE_P) == 0){
            continue;
        }

        if ((result = sys_page_map(thisenv->env_id, (void*)address , envid, (void*)address , pte & PTE_SYSCALL)) < 0){
            panic("not mapped");
        };
    }

    duppage(envid, PGNUM((USTACKTOP - PGSIZE)));

    sys_env_set_pgfault_upcall(envid,_pgfault_upcall);

    if ((result = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_W)) < 0){
        panic("sfork : sys_page_alloc: %e", result);
    }

    // Start the child environment running
    if ((result = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
        panic("sfork : sys_env_set_status: %e", result);


    return envid;
}
