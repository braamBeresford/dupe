// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

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

	// LAB 4: Your code here.
	if(!(err & FEC_WR)) // Check if a write (1)
		panic("fork pgfault recieved a non-write fault\n");
	
	if( !((uvpd[PDX(addr)] & PTE_P)) || (~uvpt[PGNUM(addr)] & (PTE_COW | PTE_P))) 
		// Check if page is CoW (2)
		panic("fork pgfault recieved a write to non CoW page\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	if((r  = sys_page_alloc(0, PFTEMP, PTE_P | PTE_U | PTE_W))) 
		// I just figured out what R was meant to be used for
		panic("fork pagefualt failed to sys_page_alloc: %e\n", r);

	void *fault_addr = (void*)ROUNDDOWN(addr, PGSIZE);
	memcpy(PFTEMP, (void*)PTE_ADDR(fault_addr), PGSIZE);

	if(sys_page_map(0, PFTEMP, 0, (void*)PTE_ADDR(fault_addr), PTE_U | PTE_P | PTE_W)){
		panic("fork pgfault failed to page_map\n");
	}

	if(sys_page_unmap(0,PFTEMP)){
		panic("fork pgfault failed to page_unmap\n");
	}
	
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
	int r;

	// LAB 4: Your code here.
	void *addr = (void *)(pn * PGSIZE);

	if (uvpt[pn] & (PTE_W | PTE_COW)){
		if (sys_page_map(0, addr, envid, addr, PTE_P|PTE_U|PTE_COW) < 0)
			panic("duppage failed to sys_page_map the envid\n");
		if (sys_page_map(0, addr, 0, addr, PTE_U|PTE_P|PTE_COW) < 0)
			panic("duppage failed to sys_page_map the envid 0\n");
	}
	else{
		if(sys_page_map(0, (void *)addr, envid, (void *)addr, PTE_P | PTE_U))
			panic("duppage failed to sys_page_map the envid for non cow\n");
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
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);

	envid_t en_vid = sys_exofork();
	if(en_vid < 0){
		panic("fork failed to run sys_exofork\n");
	}
	
	if(en_vid == 0){
		// This means we are the child
		// All page allocs are handled by parent so we can return
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	//Parent from here on out
	for (uint32_t i = 0; i < USTACKTOP; i += PGSIZE){
		if(uvpd[PDX(i)]& PTE_P && !(~uvpt[PGNUM(i)] & (PTE_P | PTE_U)))
			duppage(en_vid, i / PGSIZE);
	}


	if (sys_page_alloc(en_vid, (void *)(UXSTACKTOP - PGSIZE), PTE_P | PTE_W | PTE_U)) {
		panic("fork failed to sys_page_alloc\n");
	}

	sys_env_set_pgfault_upcall(en_vid, thisenv->env_pgfault_upcall);
	if(sys_env_set_status(en_vid, ENV_RUNNABLE))
		panic("fork failed to set env status\n");
		
	return en_vid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
