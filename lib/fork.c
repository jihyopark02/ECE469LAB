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
	if (((err & FEC_WR) == 0) || (((uvpt[PGNUM(addr)]) & PTE_COW) == 0)) {
		panic("Invalid copy-on-write page\n");
	}
	
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.

	if ((r = sys_page_alloc(sys_getenvid(), (void *) PFTEMP, PTE_P | PTE_U | PTE_W)) < 0) {
		panic("sys_page_alloc failed %e\n", r);
	}

	memmove((void *) PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);

	if ((r = sys_page_map(sys_getenvid(), (void *) PFTEMP, sys_getenvid(), ROUNDDOWN(addr, PGSIZE), PTE_P | PTE_U | PTE_W)) < 0) {
		panic("sys_page_map failed %e\n", r);
	}

	if ((r = sys_page_unmap(sys_getenvid(), (void *) PFTEMP)) < 0) {
		panic("sys_page_unmap failed %e\n", r);
	}

	//panic("pgfault not implemented");
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
	// panic("duppage not implemented");

	void* address = (void *) (pn * PGSIZE);
	pte_t pte = uvpt[pn];
	envid_t s_envid = sys_getenvid();

	if (pte & PTE_SHARE) {
		if ((r = sys_page_map(s_envid, address, envid, address, pte & PTE_SYSCALL)) < 0) {
			panic("duppage: sys_page_map failed %e\n", r);
		}
	} else if (pte & PTE_W || pte & PTE_COW) {
		if ((r = sys_page_map(s_envid, address, envid, address, PTE_COW | PTE_U | PTE_P)) < 0) {
			panic("duppage: sys_page_map failed %e\n", r);
		}

		if ((r = sys_page_map(s_envid, address, s_envid, address, PTE_COW | PTE_U | PTE_P)) < 0) {
			panic("duppage: sys_page_map failed %e\n", r);
		}
	} else {
		if ((r = sys_page_map(s_envid, address, envid, address, PTE_COW | PTE_U | PTE_P)) < 0) {
			panic("duppage: sys_page_map failed %e\n", r);
		}
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
	// panic("fork not implemented");

	set_pgfault_handler(pgfault);
	envid_t envid = sys_exofork();
	// cprintf("envid fork : %e", envid);
	// CHILD PROCESS
	if (envid < 0) {
		panic("sys_exofork failed: %d\n", envid);
	}

	if (envid == 0) {
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// PARENT PROCESS
	for (uint32_t address = 0; address <= USTACKTOP; address += PGSIZE) {
		if ((uvpd[PDX(address)] & PTE_P) == PTE_P && (uvpt[PGNUM(address)] & PTE_P) == PTE_P) {
			duppage(envid, PGNUM(address));
		}
	}

	int r;

	if ((r = sys_page_alloc(envid, (void *) (UXSTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W)) < 0) {
		panic("sys_page_alloc failed: %e\n", r);
	}

	if ((r = sys_env_set_pgfault_upcall(envid, thisenv -> env_pgfault_upcall)) < 0) {
		panic("sys_env_set_pgfault_upcall failed: %e\n", r);
	}

	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0) {
		panic("sys_env_set_status failed: %e\n", r);
	}
	
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
